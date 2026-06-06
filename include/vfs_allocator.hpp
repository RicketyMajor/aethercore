#pragma once
#include <cstdint>

// Retornan el índice numérico del bloque o i-nodo asignado, o -1 si no hay espacio
int32_t aether_allocate_block();
void aether_free_block(uint32_t block_index);

int32_t aether_allocate_inode();
void aether_free_inode(uint32_t inode_id);