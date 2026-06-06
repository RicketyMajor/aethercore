#pragma once
#include "vfs_structures.hpp"

// Funciones del ciclo de vida del VFS
bool aether_format();
void aether_cleanup();

// Variables globales externas para que todo el sistema pueda acceder al VFS
extern uint8_t *vfs_memory_block;
extern Superblock *super_block;