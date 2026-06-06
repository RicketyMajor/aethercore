#include <iostream>
#include <vector>
#include "vfs_structures.hpp"
#include "vfs_core.hpp"
#include "vfs_allocator.hpp"

int main()
{
    std::cout << "=======================================\n";
    std::cout << "  AetherCore VFS - Inicializando...\n";
    std::cout << "=======================================\n";

    if (!aether_format())
    {
        std::cerr << "[FATAL] Fallo al formatear el disco virtual.\n";
        return 1;
    }
    std::cout << "-> Sistema de Archivos montado.\n\n";

    // --- GESTOR DE MEMORIA ---
    std::cout << "Probando el Allocator...\n";
    std::cout << "Bloques libres antes de asignar: " << super_block->free_blocks << "\n";

    std::vector<int32_t> reserved_blocks;

    // Asignar 100 bloques
    for (int i = 0; i < 100; ++i)
    {
        int32_t block_id = aether_allocate_block();
        if (block_id != -1)
        {
            reserved_blocks.push_back(block_id);
        }
    }
    std::cout << "Asignados 100 bloques. Bloques libres ahora: " << super_block->free_blocks << "\n";

    // Liberar los primeros 50 bloques que se reservaron
    for (int i = 0; i < 50; ++i)
    {
        aether_free_block(reserved_blocks[i]);
    }
    std::cout << "Liberados 50 bloques. Bloques libres ahora: " << super_block->free_blocks << "\n";

    // Probar asignación de i-nodos
    int32_t new_inode = aether_allocate_inode();
    std::cout << "I-nodo asignado exitosamente. ID: " << new_inode << "\n";
    std::cout << "I-nodos libres restantes: " << super_block->free_inodes << "\n\n";

    aether_cleanup();
    return 0;
}