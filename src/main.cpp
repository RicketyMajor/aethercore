#include <iostream>
#include <vector>
#include "vfs_structures.hpp"
#include "vfs_core.hpp"
#include "vfs_allocator.hpp"
#include "vfs_hierarchy.hpp"
#include "vfs_file_io.hpp"

int main()
{
    std::cout << "=======================================\n";
    std::cout << "  AetherCore VFS - Test de Carga Final\n";
    std::cout << "=======================================\n";

    if (!aether_format())
        return 1;

    // Configurar entorno
    aether_mkdir("/db_data");
    aether_touch("/db_data/temp.log");

    std::cout << "-> Entorno preparado.\n";
    std::cout << "Bloques libres antes de escribir: " << super_block->free_blocks << "\n";

    // --- TEST DE ESCRITURA MASIVA (10 MB) ---
    int32_t fd = aether_open("/db_data/temp.log", FileMode::WRITE);
    if (fd == -1)
        return 1;

    // Crear un buffer gigante de 10 MB llenos del caracter 'A'
    uint32_t ten_mb = 10 * 1024 * 1024;
    std::vector<uint8_t> write_buffer(ten_mb, 'A');

    std::cout << "-> Escribiendo 10 MB en RAM a traves de multi-level indexing...\n";
    int32_t written = aether_write(fd, write_buffer.data(), ten_mb);
    std::cout << "   Bytes escritos: " << written << " (" << written / (1024 * 1024) << " MB)\n";

    aether_close(fd);

    std::cout << "Bloques libres tras la escritura masiva: " << super_block->free_blocks << "\n\n";

    // --- TEST DE LECTURA ---
    std::cout << "-> Leyendo el inicio y el final del archivo para verificar integridad...\n";
    int32_t fd_read = aether_open("/db_data/temp.log", FileMode::READ);

    std::vector<uint8_t> read_buffer(10); // Leemos solo 10 bytes para la prueba
    aether_read(fd_read, read_buffer.data(), 10);

    std::string read_str(read_buffer.begin(), read_buffer.end());
    std::cout << "   Primeros 10 bytes: [" << read_str << "]\n";

    aether_close(fd_read);

    aether_ls("/db_data");

    std::cout << "\n-> Eliminando el archivo gigante...\n";
    aether_rm("/db_data/temp.log");
    std::cout << "Bloques libres tras eliminar: " << super_block->free_blocks << " \n";

    aether_cleanup();
    return 0;
}