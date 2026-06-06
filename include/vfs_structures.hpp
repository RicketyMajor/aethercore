#pragma once

#include <cstdint>
#include <shared_mutex>

// ==========================================
// CONSTANTES GLOBALES DEL SISTEMA
// ==========================================
constexpr uint64_t TOTAL_RAM_SIZE = 1024ULL * 1024ULL * 1024ULL; // 1 GB de RAM
constexpr uint32_t BLOCK_SIZE = 4096;                            // 4 KB por bloque físico
constexpr uint32_t TOTAL_BLOCKS = TOTAL_RAM_SIZE / BLOCK_SIZE;

constexpr uint32_t MAX_FILENAME = 252;           // Para que DirectoryEntry encaje perfectamente en bloques de tamaño potencia de 2
constexpr uint32_t TOMBSTONE_INODE = 0xFFFFFFFF; // Representa una entrada de directorio borrada
constexpr uint32_t DIRECT_POINTERS = 12;

// Tipos de archivo
enum class FileType : uint8_t
{
    EMPTY = 0,
    FILE = 1,
    DIRECTORY = 2
};

// ==========================================
// ESTRUCTURAS DE DATOS CORE
// ==========================================

/*
 * Superbloque: Almacena la metadata global del VFS
 */
struct Superblock
{
    uint64_t total_size;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;

    // Punteros lógicos (índices de bloque) a las siguientes secciones
    uint32_t bitmap_start_block;
    uint32_t inode_table_start_block;
    uint32_t data_start_block;
};

/*
 * I-nodo: Representa un archivo o directorio de forma única
 */
struct Inode
{
    uint32_t id;
    FileType type;
    uint32_t size_bytes;
    uint16_t permissions;

    // Timestamps (UNIX epoch)
    uint64_t created_at;
    uint64_t modified_at;

    // Punteros a bloques de datos (Multi-level Indexing para soportar archivos > 4MB)
    uint32_t direct_blocks[DIRECT_POINTERS];
    uint32_t indirect_block;        // Puntero a un bloque lleno de punteros a bloques de datos
    uint32_t double_indirect_block; // Puntero a un bloque lleno de punteros a bloques indirectos

    // Mutex a nivel de i-nodo para el futuro motor concurrente (no ocupa espacio en el disco virtual, solo en la representación en RAM)
    mutable std::shared_mutex rw_lock;
};

/*
 * Entrada de Directorio: Contenido de los bloques de datos de un i-nodo tipo DIRECTORY
 */
struct DirectoryEntry
{
    uint32_t inode_id;
    char name[MAX_FILENAME];
};