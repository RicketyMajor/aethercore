#include "vfs_core.hpp"
#include <sys/mman.h>
#include <cstring>
#include <iostream>

// Definición de las variables globales
uint8_t *vfs_memory_block = nullptr;
Superblock *super_block = nullptr;

bool aether_format()
{
    // Reservar memoria usando mmap (MAP_PRIVATE y MAP_ANONYMOUS para operar 100% en RAM)
    vfs_memory_block = (uint8_t *)mmap(NULL, TOTAL_RAM_SIZE,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (vfs_memory_block == MAP_FAILED)
    {
        std::cerr << "[ERROR] No se pudo reservar " << TOTAL_RAM_SIZE / (1024 * 1024) << " MB de RAM.\n";
        return false;
    }

    // Mapear el Superbloque en el byte 0
    super_block = reinterpret_cast<Superblock *>(vfs_memory_block);
    super_block->total_size = TOTAL_RAM_SIZE;
    super_block->block_size = BLOCK_SIZE;
    super_block->total_blocks = TOTAL_BLOCKS;

    // Cálculo geométrico del disco virtual
    uint32_t superblock_blocks = 1;

    // El Bitmap necesita 1 bit por cada bloque del sistema
    uint32_t bitmap_bytes = TOTAL_BLOCKS / 8;
    uint32_t bitmap_blocks = (bitmap_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE; // División techo (Ceiling)

    // Tabla de I-nodos: Se permite un máximo de 100,000 archivos/carpetas
    uint32_t max_inodes = 100000;
    uint32_t inode_table_bytes = max_inodes * sizeof(Inode);
    uint32_t inode_table_blocks = (inode_table_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Configurar los punteros lógicos (offsets de bloques)
    super_block->bitmap_start_block = superblock_blocks;
    super_block->inode_table_start_block = super_block->bitmap_start_block + bitmap_blocks;
    super_block->data_start_block = super_block->inode_table_start_block + inode_table_blocks;

    super_block->total_inodes = max_inodes;
    super_block->free_inodes = max_inodes - 1; // El directorio raíz (/) usará el primero

    // Inicializar el Bitmap
    uint8_t *bitmap_ptr = vfs_memory_block + (super_block->bitmap_start_block * BLOCK_SIZE);

    // Poner todo en 0xFF (11111111), lo que significa que todos los bloques están libres inicialmente
    std::memset(bitmap_ptr, 0xFF, bitmap_bytes);

    // Ahora, marcar como ocupados (0) los bloques estructurales: Superbloque, Bitmap y Tabla de I-nodos
    uint32_t reserved_blocks = super_block->data_start_block;
    super_block->free_blocks = TOTAL_BLOCKS - reserved_blocks;

    for (uint32_t i = 0; i < reserved_blocks; ++i)
    {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        bitmap_ptr[byte_idx] &= ~(1 << (7 - bit_idx)); // Operación Bitwise para apagar el bit (dejarlo en 0)
    }

    // Inicializar la Tabla de I-nodos en 0
    uint8_t *inode_table_ptr = vfs_memory_block + (super_block->inode_table_start_block * BLOCK_SIZE);
    std::memset(inode_table_ptr, 0, inode_table_blocks * BLOCK_SIZE);

    // Crear el Directorio Raíz (/) asignándole el I-nodo 0
    Inode *root_inode = reinterpret_cast<Inode *>(inode_table_ptr); // El primer i-nodo de la tabla
    root_inode->id = 0;
    root_inode->type = FileType::DIRECTORY;
    root_inode->size_bytes = 0;
    root_inode->permissions = 0755; // Permisos estilo UNIX (rwxr-xr-x)

    std::cout << "[OK] VFS formateado exitosamente. Bloques libres: " << super_block->free_blocks << "\n";
    return true;
}

void aether_cleanup()
{
    if (vfs_memory_block != nullptr && vfs_memory_block != MAP_FAILED)
    {
        munmap(vfs_memory_block, TOTAL_RAM_SIZE); // Devuelve la RAM al SO anfitrión
        std::cout << "[OK] Memoria VFS liberada correctamente. Cero Memory Leaks.\n";
    }
}