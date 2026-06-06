#include "vfs_hierarchy.hpp"
#include "vfs_core.hpp"
#include "vfs_allocator.hpp"
#include "vfs_structures.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <shared_mutex>

// ==========================================
// FUNCIONES AUXILIARES (HELPERS)
// ==========================================

// Retorna el puntero directo a la memoria de un bloque físico
static uint8_t *get_block_ptr(uint32_t block_index)
{
    return vfs_memory_block + (block_index * BLOCK_SIZE);
}

// Retorna el puntero directo a un i-nodo en la tabla
static Inode *get_inode_ptr(uint32_t inode_id)
{
    if (inode_id >= super_block->total_inodes)
        return nullptr;
    Inode *inode_table = reinterpret_cast<Inode *>(vfs_memory_block + (super_block->inode_table_start_block * BLOCK_SIZE));
    return &inode_table[inode_id];
}

// Divide una ruta "/db_data/temp.log" en un vector ["db_data", "temp.log"]
static std::vector<std::string> split_path(const std::string &path)
{
    std::vector<std::string> tokens;
    std::stringstream ss(path);
    std::string token;
    while (std::getline(ss, token, '/'))
    {
        if (!token.empty())
            tokens.push_back(token);
    }
    return tokens;
}

// Busca un nombre dentro de los bloques de datos de un i-nodo directorio. Retorna el ID del i-nodo hijo o -1.
static int32_t find_in_directory(Inode *dir_inode, const std::string &name)
{
    if (dir_inode->type != FileType::DIRECTORY)
        return -1;

    uint32_t entry_size = sizeof(DirectoryEntry);
    uint32_t total_entries = dir_inode->size_bytes / entry_size;
    uint32_t entries_checked = 0;

    std::shared_lock<std::shared_mutex> lock(dir_inode->rw_lock); // Read lock

    // busca solo en los punteros directos
    for (int i = 0; i < DIRECT_POINTERS && entries_checked < total_entries; ++i)
    {
        if (dir_inode->direct_blocks[i] == 0)
            continue;

        uint8_t *block_ptr = get_block_ptr(dir_inode->direct_blocks[i]);
        DirectoryEntry *entries = reinterpret_cast<DirectoryEntry *>(block_ptr);

        uint32_t max_entries_in_block = BLOCK_SIZE / entry_size;
        uint32_t entries_in_this_block = std::min(max_entries_in_block, total_entries - entries_checked);

        for (uint32_t j = 0; j < entries_in_this_block; ++j)
        {
            if (name == entries[j].name)
                return entries[j].inode_id;
            entries_checked++;
        }
    }
    return -1;
}

// Navega desde la raíz para encontrar el directorio padre de una ruta. Modifica 'out_filename' con el último nombre.
static int32_t resolve_parent_directory(const std::string &path, std::string &out_filename)
{
    std::vector<std::string> tokens = split_path(path);
    if (tokens.empty())
        return -1; // Es la raíz "/"

    out_filename = tokens.back(); // El elemento a crear (ej. "temp.log")
    tokens.pop_back();

    int32_t current_inode_id = 0; // Comienza siempre en root (ID 0)
    for (const auto &token : tokens)
    {
        Inode *curr_inode = get_inode_ptr(current_inode_id);
        current_inode_id = find_in_directory(curr_inode, token);
        if (current_inode_id == -1)
            return -1; // Ruta intermedia no existe
    }
    return current_inode_id;
}

// Agrega una nueva entrada al directorio padre
static bool add_entry_to_dir(Inode *parent_dir, uint32_t child_inode_id, const std::string &name)
{
    std::unique_lock<std::shared_mutex> lock(parent_dir->rw_lock); // Write lock

    uint32_t entry_size = sizeof(DirectoryEntry);
    uint32_t max_entries_per_block = BLOCK_SIZE / entry_size;
    uint32_t current_entries = parent_dir->size_bytes / entry_size;

    uint32_t block_index = current_entries / max_entries_per_block;
    uint32_t offset_in_block = (current_entries % max_entries_per_block) * entry_size;

    if (block_index >= DIRECT_POINTERS)
    {
        std::cerr << "[ERROR] Directorio lleno (Indireccion no implementada en Fase 3).\n";
        return false;
    }

    // Si no hay bloque asignado en este índice, se pide
    if (parent_dir->direct_blocks[block_index] == 0)
    {
        int32_t new_block = aether_allocate_block();
        if (new_block == -1)
            return false;
        parent_dir->direct_blocks[block_index] = new_block;
    }

    // Escribir la entrada
    uint8_t *block_ptr = get_block_ptr(parent_dir->direct_blocks[block_index]);
    DirectoryEntry *new_entry = reinterpret_cast<DirectoryEntry *>(block_ptr + offset_in_block);

    new_entry->inode_id = child_inode_id;
    std::strncpy(new_entry->name, name.c_str(), MAX_FILENAME - 1);
    new_entry->name[MAX_FILENAME - 1] = '\0'; // Asegurar null-termination

    parent_dir->size_bytes += entry_size;
    return true;
}

// ==========================================
// IMPLEMENTACIÓN DE LA API
// ==========================================

bool aether_mkdir(const std::string &path)
{
    std::string new_dir_name;
    int32_t parent_id = resolve_parent_directory(path, new_dir_name);

    if (parent_id == -1)
    {
        std::cerr << "[ERROR] Ruta padre no encontrada para: " << path << "\n";
        return false;
    }

    Inode *parent_inode = get_inode_ptr(parent_id);
    if (find_in_directory(parent_inode, new_dir_name) != -1)
    {
        std::cerr << "[ERROR] El directorio ya existe: " << new_dir_name << "\n";
        return false;
    }

    int32_t new_inode_id = aether_allocate_inode();
    if (new_inode_id == -1)
        return false;

    Inode *new_inode = get_inode_ptr(new_inode_id);
    new_inode->type = FileType::DIRECTORY;
    new_inode->size_bytes = 0;
    new_inode->permissions = 0755;
    std::memset(new_inode->direct_blocks, 0, sizeof(new_inode->direct_blocks));

    // Agregar . y .. en el nuevo directorio
    add_entry_to_dir(new_inode, new_inode_id, ".");
    add_entry_to_dir(new_inode, parent_id, "..");

    // Vincularlo en el padre
    return add_entry_to_dir(parent_inode, new_inode_id, new_dir_name);
}

bool aether_touch(const std::string &path)
{
    std::string new_file_name;
    int32_t parent_id = resolve_parent_directory(path, new_file_name);

    if (parent_id == -1)
    {
        std::cerr << "[ERROR] Ruta padre no encontrada para: " << path << "\n";
        return false;
    }

    Inode *parent_inode = get_inode_ptr(parent_id);
    if (find_in_directory(parent_inode, new_file_name) != -1)
    {
        // Touch normalmente actualiza el timestamp, aquí solo evitaremos crearlo si existe
        return true;
    }

    int32_t new_inode_id = aether_allocate_inode();
    if (new_inode_id == -1)
        return false;

    Inode *new_inode = get_inode_ptr(new_inode_id);
    new_inode->type = FileType::FILE;
    new_inode->size_bytes = 0; // Archivo vacío no ocupa bloques de datos aún
    new_inode->permissions = 0644;
    std::memset(new_inode->direct_blocks, 0, sizeof(new_inode->direct_blocks));

    return add_entry_to_dir(parent_inode, new_inode_id, new_file_name);
}

void aether_ls(const std::string &path)
{
    std::string dummy;
    int32_t target_id = 0; // Se asume root por defecto

    if (path != "/" && path != "")
    {
        target_id = resolve_parent_directory(path + "/dummy", dummy);
    }

    if (target_id == -1)
    {
        std::cerr << "ls: no se puede acceder a '" << path << "': No existe el archivo o el directorio\n";
        return;
    }

    Inode *dir_inode = get_inode_ptr(target_id);
    if (dir_inode->type != FileType::DIRECTORY)
    {
        std::cerr << "ls: '" << path << "' no es un directorio\n";
        return;
    }

    std::shared_lock<std::shared_mutex> lock(dir_inode->rw_lock);

    uint32_t entry_size = sizeof(DirectoryEntry);
    uint32_t total_entries = dir_inode->size_bytes / entry_size;
    uint32_t entries_checked = 0;

    std::cout << "Contenido de " << (path.empty() ? "/" : path) << ":\n";
    std::cout << std::left << std::setw(10) << "INODE" << std::setw(10) << "TIPO" << std::setw(10) << "TAMAÑO" << "NOMBRE\n";
    std::cout << "------------------------------------------------------\n";

    for (int i = 0; i < DIRECT_POINTERS && entries_checked < total_entries; ++i)
    {
        if (dir_inode->direct_blocks[i] == 0)
            continue;

        uint8_t *block_ptr = get_block_ptr(dir_inode->direct_blocks[i]);
        DirectoryEntry *entries = reinterpret_cast<DirectoryEntry *>(block_ptr);

        uint32_t max_entries_in_block = BLOCK_SIZE / entry_size;
        uint32_t entries_in_this_block = std::min(max_entries_in_block, total_entries - entries_checked);

        for (uint32_t j = 0; j < entries_in_this_block; ++j)
        {
            if (entries[j].inode_id == TOMBSTONE_INODE)
            {
                entries_checked++;
                continue; // Saltar entradas borradas
            }

            Inode *child = get_inode_ptr(entries[j].inode_id);
            std::string type_str = (child->type == FileType::DIRECTORY) ? "DIR" : "FILE";

            std::cout << std::left << std::setw(10) << entries[j].inode_id
                      << std::setw(10) << type_str
                      << std::setw(10) << child->size_bytes
                      << entries[j].name << "\n";
            entries_checked++;
        }
    }
    std::cout << "\n";
}

int32_t aether_find_inode_by_path(const std::string &path)
{
    if (path == "/" || path.empty())
        return 0; // Raíz

    std::vector<std::string> tokens = split_path(path);
    int32_t current_inode_id = 0; // Comenzar en root

    for (const auto &token : tokens)
    {
        Inode *curr_inode = get_inode_ptr(current_inode_id);
        if (!curr_inode)
            return -1;

        current_inode_id = find_in_directory(curr_inode, token);
        if (current_inode_id == -1)
        {
            return -1; // Algún componente de la ruta no existe
        }
    }

    return current_inode_id;
}

bool aether_unlink_entry(const std::string &path)
{
    std::string target_name;
    int32_t parent_id = resolve_parent_directory(path, target_name);

    if (parent_id == -1)
        return false;

    Inode *parent_inode = get_inode_ptr(parent_id);
    std::unique_lock<std::shared_mutex> lock(parent_inode->rw_lock); // Bloqueo exclusivo

    uint32_t entry_size = sizeof(DirectoryEntry);
    uint32_t total_entries = parent_inode->size_bytes / entry_size;
    uint32_t entries_checked = 0;

    for (int i = 0; i < DIRECT_POINTERS && entries_checked < total_entries; ++i)
    {
        if (parent_inode->direct_blocks[i] == 0)
            continue;

        uint8_t *block_ptr = get_block_ptr(parent_inode->direct_blocks[i]);
        DirectoryEntry *entries = reinterpret_cast<DirectoryEntry *>(block_ptr);

        uint32_t max_entries_in_block = BLOCK_SIZE / entry_size;
        uint32_t entries_in_this_block = std::min(max_entries_in_block, total_entries - entries_checked);

        for (uint32_t j = 0; j < entries_in_this_block; ++j)
        {
            // Busca la entrada ignorando los tombstones
            if (entries[j].inode_id != TOMBSTONE_INODE && entries[j].name == target_name)
            {
                entries[j].inode_id = TOMBSTONE_INODE; // Marca como Lápida
                return true;
            }
            entries_checked++;
        }
    }
    return false;
}