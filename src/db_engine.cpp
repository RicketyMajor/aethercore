#include "db_engine.hpp"
#include "vfs_hierarchy.hpp"
#include "vfs_file_io.hpp"
#include <iostream>
#include <vector>
#include <cstring>

// Define un "Magic Number" para identificar que el archivo .idx pertenece a AetherCore (Hex: AEC0RE)
constexpr uint32_t AETHER_MAGIC_NUMBER = 0xAE0C00E0;

AetherDatabase::AetherDatabase(const std::string &name, uint32_t num_threads)
    : db_name(name)
{

    // Instanciar el Thread Pool
    thread_pool = std::make_unique<AetherThreadPool>(num_threads);

    // Definir las rutas absolutas dentro del VFS
    idx_path = "/" + db_name + "/" + db_name + ".idx";
    dat_path = "/" + db_name + "/" + db_name + ".dat";
}

AetherDatabase::~AetherDatabase()
{
    // El unique_ptr destruirá el Thread Pool automáticamente, apagando los hilos limpiamente
}

bool AetherDatabase::format_db()
{
    std::cout << "[DB] Formateando base de datos '" << db_name << "' en el VFS...\n";

    // Crear el subdirectorio de la base de datos
    std::string dir_path = "/" + db_name;
    if (!aether_mkdir(dir_path))
    {
        // Si falla, verifica si ya existe (puede ser un reinicio)
        if (aether_find_inode_by_path(dir_path) == -1)
        {
            std::cerr << "[DB ERROR] No se pudo crear el directorio de la base de datos.\n";
            return false;
        }
    }

    // Crear los archivos físicos virtuales
    aether_touch(idx_path);
    aether_touch(dat_path);

    // Pre-asignar y formatear el archivo de Índice (.idx)
    int32_t fd_idx = aether_open(idx_path, FileMode::WRITE);
    if (fd_idx == -1)
        return false;

    // Preparar y escribir la Cabecera (Header)
    IndexHeader header;
    header.magic_number = AETHER_MAGIC_NUMBER;
    header.total_records = 0;
    aether_write(fd_idx, reinterpret_cast<const uint8_t *>(&header), sizeof(IndexHeader));

    // Preparar la Tabla Hash (Array de IndexRecords vacíos)
    std::cout << "[DB] Pre-asignando " << HASH_TABLE_BUCKETS << " buckets para la tabla Hash...\n";
    std::vector<IndexRecord> empty_buckets(HASH_TABLE_BUCKETS);

    for (uint32_t i = 0; i < HASH_TABLE_BUCKETS; ++i)
    {
        empty_buckets[i].key_hash = 0;
        empty_buckets[i].data_offset = TOMBSTONE_OFFSET; // Marca de vacío
        empty_buckets[i].key_len = 0;
        std::memset(empty_buckets[i].key, 0, MAX_KEY_LEN);
    }

    // Escribir todos los buckets de un solo golpe (Aprovechando la eficiencia de nuestro VFS)
    uint32_t bytes_to_write = HASH_TABLE_BUCKETS * sizeof(IndexRecord);
    int32_t written = aether_write(fd_idx, reinterpret_cast<const uint8_t *>(empty_buckets.data()), bytes_to_write);

    aether_close(fd_idx);

    if (written != bytes_to_write)
    {
        std::cerr << "[DB ERROR] Fallo al pre-asignar el espacio de la tabla Hash.\n";
        return false;
    }

    // El archivo .dat se deja vacío (0 bytes), crecerá bajo demanda (Append-only)

    std::cout << "[DB] Formateo completado. Archivo de índice (.idx) configurado con "
              << bytes_to_write / 1024 << " KB pre-asignados.\n";

    return true;
}