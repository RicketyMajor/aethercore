#include <iostream>
#include "vfs_structures.hpp"

int main()
{
    std::cout << "=======================================\n";
    std::cout << "  AetherCore VFS - Inicializando...\n";
    std::cout << "=======================================\n";

    std::cout << "Parametros del Sistema:\n";
    std::cout << "- Memoria Total: " << TOTAL_RAM_SIZE / (1024 * 1024) << " MB\n";
    std::cout << "- Tamano de Bloque: " << BLOCK_SIZE << " Bytes\n";
    std::cout << "- Total de Bloques: " << TOTAL_BLOCKS << "\n";
    std::cout << "- Tamano de Inode: " << sizeof(Inode) << " Bytes\n";

    // TODO: Llamar a aether_format() en la Fase 1

    return 0;
}