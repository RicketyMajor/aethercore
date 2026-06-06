#include <iostream>
#include "vfs_structures.hpp"
#include "vfs_core.hpp"

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

    // Formateo lógico
    if (!aether_format())
    {
        std::cerr << "[FATAL] Fallo al formatear el disco virtual en memoria.\n";
        return 1;
    }

    std::cout << "-> Sistema de Archivos montado y listo.\n\n";

    // Limpieza final (crucial para evitar destrozar la RAM de tu PC)
    aether_cleanup();

    return 0;
}