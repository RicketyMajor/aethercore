#include "vfs_allocator.hpp"
#include "vfs_core.hpp"
#include "vfs_structures.hpp"

int32_t aether_allocate_block()
{
    if (super_block->free_blocks == 0)
        return -1; // Disco virtual lleno

    uint8_t *bitmap_ptr = vfs_memory_block + (super_block->bitmap_start_block * BLOCK_SIZE);
    uint32_t total_bytes = super_block->total_blocks / 8;

    // Recorre el Bitmap byte a byte
    for (uint32_t i = 0; i < total_bytes; ++i)
    {
        if (bitmap_ptr[i] != 0x00)
        { // Si el byte no es 0, hay al menos un bit en 1 (bloque libre)
            // Recorre los 8 bits del byte (de izquierda a derecha: 7 a 0)
            for (int bit = 7; bit >= 0; --bit)
            {
                if (bitmap_ptr[i] & (1 << bit))
                {
                    // Encuentra el bit libre. Lo apaga (o sea, lo marca como 0 ocupado)
                    bitmap_ptr[i] &= ~(1 << bit);
                    super_block->free_blocks--;

                    // Calcula y devuelve el índice global del bloque
                    return (i * 8) + (7 - bit);
                }
            }
        }
    }
    return -1;
}

void aether_free_block(uint32_t block_index)
{
    if (block_index >= super_block->total_blocks)
        return;

    uint8_t *bitmap_ptr = vfs_memory_block + (super_block->bitmap_start_block * BLOCK_SIZE);
    uint32_t byte_idx = block_index / 8;
    uint32_t bit_idx = block_index % 8;

    // Verifica si el bit realmente estaba en 0 (ocupado) antes de liberarlo
    if ((bitmap_ptr[byte_idx] & (1 << (7 - bit_idx))) == 0)
    {
        bitmap_ptr[byte_idx] |= (1 << (7 - bit_idx)); // Lo enciende (o sea, lo marca como 1 libre)
        super_block->free_blocks++;
    }
}

int32_t aether_allocate_inode()
{
    if (super_block->free_inodes == 0)
        return -1;

    Inode *inode_table = reinterpret_cast<Inode *>(vfs_memory_block + (super_block->inode_table_start_block * BLOCK_SIZE));

    // Implementa aether_allocate_inode() para buscar en la tabla de i-nodos el primer i-nodo que no esté en uso
    for (uint32_t i = 0; i < super_block->total_inodes; ++i)
    {
        if (inode_table[i].type == FileType::EMPTY)
        {
            inode_table[i].type = FileType::FILE; // Lo marca temporalmente para reservarlo
            super_block->free_inodes--;
            return i;
        }
    }
    return -1;
}

void aether_free_inode(uint32_t inode_id)
{
    if (inode_id >= super_block->total_inodes)
        return;

    Inode *inode_table = reinterpret_cast<Inode *>(vfs_memory_block + (super_block->inode_table_start_block * BLOCK_SIZE));

    if (inode_table[inode_id].type != FileType::EMPTY)
    {
        inode_table[inode_id].type = FileType::EMPTY; // Lo devuelve al pool
        super_block->free_inodes++;
    }
}