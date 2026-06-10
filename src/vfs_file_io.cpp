#include "vfs_file_io.hpp"
#include "vfs_core.hpp"
#include "vfs_structures.hpp"
#include "vfs_allocator.hpp"
#include "vfs_hierarchy.hpp"
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <iostream>
#include <algorithm>

// ==========================================
// ESTRUCTURAS INTERNAS Y ESTADO GLOBAL
// ==========================================
struct OpenFileDescriptor
{
    int32_t inode_id;
    FileMode mode;
    uint32_t offset;
};

// Tabla de archivos abiertos global (y su mutex para thread-safety)
static std::unordered_map<int32_t, OpenFileDescriptor> open_file_table;
static std::mutex oft_mutex;
static int32_t next_fd = 100; // empieza en 100 para simular descriptores reales

// Funciones Helper para obtener punteros a la memoria física
static uint8_t *get_block_ptr(uint32_t block_index)
{
    return vfs_memory_block + (block_index * BLOCK_SIZE);
}
static Inode *get_inode_ptr(uint32_t inode_id)
{
    Inode *inode_table = reinterpret_cast<Inode *>(vfs_memory_block + (super_block->inode_table_start_block * BLOCK_SIZE));
    return &inode_table[inode_id];
}

// ==========================================
// MOTOR DE MULTI-LEVEL INDEXING
// ==========================================
// Traduce un bloque lógico (0, 1, 2...) al bloque físico real en la RAM, asignando memoria si es necesario.
static uint32_t get_or_allocate_physical_block(Inode *inode, uint32_t logical_block, bool allocate_if_missing)
{
    const uint32_t PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(uint32_t); // 1024 punteros en 4KB

    // Punteros Directos (Bloques 0 a 11)
    if (logical_block < DIRECT_POINTERS)
    {
        if (inode->direct_blocks[logical_block] == 0 && allocate_if_missing)
        {
            inode->direct_blocks[logical_block] = aether_allocate_block();
        }
        return inode->direct_blocks[logical_block];
    }

    // Indirección Simple (Bloques 12 a 1035)
    uint32_t indirect_logical = logical_block - DIRECT_POINTERS;
    if (indirect_logical < PTRS_PER_BLOCK)
    {
        if (inode->indirect_block == 0 && allocate_if_missing)
        {
            inode->indirect_block = aether_allocate_block();
            std::memset(get_block_ptr(inode->indirect_block), 0, BLOCK_SIZE);
        }
        if (inode->indirect_block == 0)
            return 0;

        uint32_t *indirect_array = reinterpret_cast<uint32_t *>(get_block_ptr(inode->indirect_block));
        if (indirect_array[indirect_logical] == 0 && allocate_if_missing)
        {
            indirect_array[indirect_logical] = aether_allocate_block();
        }
        return indirect_array[indirect_logical];
    }

    // Indirección Doble (Soporte para archivos de hasta 4 GB)
    uint32_t double_logical = indirect_logical - PTRS_PER_BLOCK;
    uint32_t l1_index = double_logical / PTRS_PER_BLOCK;
    uint32_t l2_index = double_logical % PTRS_PER_BLOCK;

    if (inode->double_indirect_block == 0 && allocate_if_missing)
    {
        inode->double_indirect_block = aether_allocate_block();
        std::memset(get_block_ptr(inode->double_indirect_block), 0, BLOCK_SIZE);
    }
    if (inode->double_indirect_block == 0)
        return 0;

    uint32_t *l1_array = reinterpret_cast<uint32_t *>(get_block_ptr(inode->double_indirect_block));

    if (l1_array[l1_index] == 0 && allocate_if_missing)
    {
        l1_array[l1_index] = aether_allocate_block();
        std::memset(get_block_ptr(l1_array[l1_index]), 0, BLOCK_SIZE);
    }
    if (l1_array[l1_index] == 0)
        return 0;

    uint32_t *l2_array = reinterpret_cast<uint32_t *>(get_block_ptr(l1_array[l1_index]));

    if (l2_array[l2_index] == 0 && allocate_if_missing)
    {
        l2_array[l2_index] = aether_allocate_block();
    }
    return l2_array[l2_index];
}

// ==========================================
// IMPLEMENTACIÓN DE LA API
// ==========================================

int32_t aether_open(const std::string &path, FileMode mode)
{
    int32_t inode_id = aether_find_inode_by_path(path);
    if (inode_id == -1)
    {
        std::cerr << "[ERROR] Archivo no encontrado: " << path << "\n";
        return -1;
    }

    Inode *inode = get_inode_ptr(inode_id);
    if (inode->type != FileType::FILE)
    {
        std::cerr << "[ERROR] La ruta no es un archivo válido.\n";
        return -1;
    }

    std::lock_guard<std::mutex> lock(oft_mutex);
    int32_t fd = next_fd++;

    uint32_t initial_offset = (mode == FileMode::APPEND) ? inode->size_bytes : 0;

    // Si se abre en modo WRITE (no append), se trunca el archivo a 0 (omitido para simplificar por ahora)

    open_file_table[fd] = {inode_id, mode, initial_offset};
    return fd;
}

void aether_close(int32_t fd)
{
    std::lock_guard<std::mutex> lock(oft_mutex);
    open_file_table.erase(fd);
}

int32_t aether_write(int32_t fd, const uint8_t *buffer, uint32_t bytes)
{
    OpenFileDescriptor ofd;
    {
        std::lock_guard<std::mutex> lock(oft_mutex);
        auto it = open_file_table.find(fd);
        if (it == open_file_table.end() || it->second.mode == FileMode::READ)
            return -1;
        ofd = it->second;
    }

    Inode *inode = get_inode_ptr(ofd.inode_id);
    std::unique_lock<std::shared_mutex> lock(inode->rw_lock); // Bloqueo exclusivo para escribir

    uint32_t bytes_written = 0;
    while (bytes_written < bytes)
    {
        uint32_t logical_block = ofd.offset / BLOCK_SIZE;
        uint32_t offset_in_block = ofd.offset % BLOCK_SIZE;
        uint32_t bytes_to_write = std::min(bytes - bytes_written, BLOCK_SIZE - offset_in_block);

        uint32_t physical_block = get_or_allocate_physical_block(inode, logical_block, true);
        if (physical_block == 0)
            break; // Sin espacio en el disco virtual

        uint8_t *dest_ptr = get_block_ptr(physical_block) + offset_in_block;
        std::memcpy(dest_ptr, buffer + bytes_written, bytes_to_write);

        ofd.offset += bytes_to_write;
        bytes_written += bytes_to_write;
        if (ofd.offset > inode->size_bytes)
        {
            inode->size_bytes = ofd.offset;
        }
    }

    lock.unlock();
    // Actualizar el offset en la tabla global
    std::lock_guard<std::mutex> oft_lock(oft_mutex);
    open_file_table[fd].offset = ofd.offset;

    return bytes_written;
}

int32_t aether_read(int32_t fd, uint8_t *buffer, uint32_t bytes)
{
    OpenFileDescriptor ofd;
    {
        std::lock_guard<std::mutex> lock(oft_mutex);
        auto it = open_file_table.find(fd);
        if (it == open_file_table.end() || it->second.mode == FileMode::WRITE)
            return -1;
        ofd = it->second;
    }

    Inode *inode = get_inode_ptr(ofd.inode_id);
    std::shared_lock<std::shared_mutex> lock(inode->rw_lock); // Bloqueo compartido para leer

    if (ofd.offset >= inode->size_bytes)
        return 0; // EOF
    uint32_t bytes_to_read_total = std::min(bytes, inode->size_bytes - ofd.offset);
    uint32_t bytes_read = 0;

    while (bytes_read < bytes_to_read_total)
    {
        uint32_t logical_block = ofd.offset / BLOCK_SIZE;
        uint32_t offset_in_block = ofd.offset % BLOCK_SIZE;
        uint32_t bytes_to_read_chunk = std::min(bytes_to_read_total - bytes_read, BLOCK_SIZE - offset_in_block);

        uint32_t physical_block = get_or_allocate_physical_block(inode, logical_block, false);

        if (physical_block != 0)
        {
            uint8_t *src_ptr = get_block_ptr(physical_block) + offset_in_block;
            std::memcpy(buffer + bytes_read, src_ptr, bytes_to_read_chunk);
        }
        else
        {
            // Si el bloque no está asignado pero está dentro del size, lee ceros (Sparse File)
            std::memset(buffer + bytes_read, 0, bytes_to_read_chunk);
        }

        ofd.offset += bytes_to_read_chunk;
        bytes_read += bytes_to_read_chunk;
    }

    lock.unlock();

    std::lock_guard<std::mutex> oft_lock(oft_mutex);
    open_file_table[fd].offset = ofd.offset;

    return bytes_read;
}

static void free_inode_blocks(Inode *inode)
{
    const uint32_t PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(uint32_t);

    // Liberar bloques directos
    for (int i = 0; i < DIRECT_POINTERS; ++i)
    {
        if (inode->direct_blocks[i] != 0)
        {
            aether_free_block(inode->direct_blocks[i]);
            inode->direct_blocks[i] = 0;
        }
    }

    // Liberar indirección simple
    if (inode->indirect_block != 0)
    {
        uint32_t *indirect_array = reinterpret_cast<uint32_t *>(get_block_ptr(inode->indirect_block));
        for (uint32_t i = 0; i < PTRS_PER_BLOCK; ++i)
        {
            if (indirect_array[i] != 0)
                aether_free_block(indirect_array[i]);
        }
        aether_free_block(inode->indirect_block);
        inode->indirect_block = 0;
    }

    // Liberar indirección doble
    if (inode->double_indirect_block != 0)
    {
        uint32_t *l1_array = reinterpret_cast<uint32_t *>(get_block_ptr(inode->double_indirect_block));
        for (uint32_t i = 0; i < PTRS_PER_BLOCK; ++i)
        {
            if (l1_array[i] != 0)
            {
                uint32_t *l2_array = reinterpret_cast<uint32_t *>(get_block_ptr(l1_array[i]));
                for (uint32_t j = 0; j < PTRS_PER_BLOCK; ++j)
                {
                    if (l2_array[j] != 0)
                        aether_free_block(l2_array[j]);
                }
                aether_free_block(l1_array[i]);
            }
        }
        aether_free_block(inode->double_indirect_block);
        inode->double_indirect_block = 0;
    }
    inode->size_bytes = 0;
}

bool aether_rm(const std::string &path)
{
    int32_t target_id = aether_find_inode_by_path(path);
    if (target_id == -1)
    {
        std::cerr << "[ERROR] Archivo no encontrado para eliminar: " << path << "\n";
        return false;
    }

    // Eliminar la entrada del directorio padre primero
    if (!aether_unlink_entry(path))
    {
        std::cerr << "[ERROR] No se pudo desvincular del directorio padre.\n";
        return false;
    }

    Inode *target_inode = get_inode_ptr(target_id);
    std::unique_lock<std::shared_mutex> lock(target_inode->rw_lock);

    // Recuperar bloques físicos y Liberar I-nodo
    free_inode_blocks(target_inode);
    aether_free_inode(target_id);

    return true;
}

void aether_seek(int32_t fd, uint32_t offset)
{
    std::lock_guard<std::mutex> lock(oft_mutex);
    auto it = open_file_table.find(fd);
    if (it != open_file_table.end())
    {
        it->second.offset = offset;
    }
}

uint32_t aether_tell(int32_t fd)
{
    std::lock_guard<std::mutex> lock(oft_mutex);
    auto it = open_file_table.find(fd);
    if (it != open_file_table.end())
    {
        return it->second.offset;
    }
    return 0;
}