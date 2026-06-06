#include <iostream>
#include "vfs_structures.hpp"
#include "vfs_core.hpp"
#include "vfs_allocator.hpp"
#include "vfs_hierarchy.hpp"

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

    // --- PRUEBA DE FASE 3: JERARQUÍA Y V-NODES ---
    std::cout << "Creando estructura de directorios y archivos...\n\n";

    // Recreando el checklist final de la Parte 1
    std::cout << "-> Ejecutando aether_mkdir(\"/db_data\")\n";
    aether_mkdir("/db_data");

    std::cout << "-> Ejecutando aether_mkdir(\"/db_data/cache\")\n";
    aether_mkdir("/db_data/cache");

    std::cout << "-> Ejecutando aether_touch(\"/db_data/temp.log\")\n";
    aether_touch("/db_data/temp.log");

    std::cout << "-> Ejecutando aether_touch(\"/db_data/config.ini\")\n\n";
    aether_touch("/db_data/config.ini");

    // Listar la raíz
    aether_ls("/");

    // Listar la carpeta creada
    aether_ls("/db_data");

    aether_cleanup();
    return 0;
}