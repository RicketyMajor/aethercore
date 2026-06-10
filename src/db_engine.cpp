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

// ==========================================
// MOTOR DE HASH Y OPERACIONES INTERNAS
// ==========================================

// Algoritmo FNV-1a: Extremadamente rápido y con gran dispersión para strings
uint32_t AetherDatabase::calculate_hash(const std::string &key)
{
    uint32_t hash = 2166136261u;
    for (char c : key)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

DBResponse AetherDatabase::internal_store(std::string key, std::string payload)
{
    DBResponse response;
    response.status = DBStatus::ERROR;
    if (key.length() >= MAX_KEY_LEN || payload.length() >= MAX_PAYLOAD_SIZE)
        return response;

    uint32_t hash = calculate_hash(key);

    // Direccionamiento Abierto (Linear Probing) para colisiones
    for (uint32_t i = 0; i < HASH_TABLE_BUCKETS; ++i)
    {
        uint32_t bucket_idx = (hash + i) % HASH_TABLE_BUCKETS;

        // Bloqueo Exclusivo (Write Lock) SOLO para este bucket específico
        std::unique_lock<std::shared_mutex> lock(bucket_locks[bucket_idx].rw_lock);

        int32_t fd_idx = aether_open(idx_path, FileMode::READ);
        aether_seek(fd_idx, sizeof(IndexHeader) + bucket_idx * sizeof(IndexRecord));
        IndexRecord record;
        aether_read(fd_idx, reinterpret_cast<uint8_t *>(&record), sizeof(IndexRecord));
        aether_close(fd_idx);

        bool is_empty = (record.key_len == 0 && record.data_offset == TOMBSTONE_OFFSET);
        bool is_match = (record.key_len == key.length() && std::strncmp(record.key, key.c_str(), key.length()) == 0);

        if (is_empty || is_match)
        {
            // Guardar Payload en .dat (Append Only)
            int32_t fd_dat = aether_open(dat_path, FileMode::APPEND);
            uint32_t payload_offset = aether_tell(fd_dat);

            DataRecordHeader d_header;
            d_header.payload_len = payload.length();
            aether_write(fd_dat, reinterpret_cast<const uint8_t *>(&d_header), sizeof(DataRecordHeader));
            aether_write(fd_dat, reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());
            aether_close(fd_dat);

            // Actualizar Tabla Hash en .idx
            record.key_hash = hash;
            record.data_offset = payload_offset;
            record.key_len = key.length();
            std::memset(record.key, 0, MAX_KEY_LEN);
            std::strncpy(record.key, key.c_str(), MAX_KEY_LEN - 1);

            int32_t fd_idx_write = aether_open(idx_path, FileMode::WRITE);
            aether_seek(fd_idx_write, sizeof(IndexHeader) + bucket_idx * sizeof(IndexRecord));
            aether_write(fd_idx_write, reinterpret_cast<const uint8_t *>(&record), sizeof(IndexRecord));
            aether_close(fd_idx_write);

            response.status = DBStatus::OK;
            response.payload_len = 0;
            return response; // Al salir del if, el unique_lock se destruye automáticamente liberando el candado
        }
    }
    return response; // DB Llena
}

DBResponse AetherDatabase::internal_fetch(std::string key)
{
    DBResponse response;
    response.status = DBStatus::NOT_FOUND;
    uint32_t hash = calculate_hash(key);

    for (uint32_t i = 0; i < HASH_TABLE_BUCKETS; ++i)
    {
        uint32_t bucket_idx = (hash + i) % HASH_TABLE_BUCKETS;

        // Bloqueo Compartido (Read Lock): Miles de hilos pueden leer el mismo bucket a la vez
        std::shared_lock<std::shared_mutex> lock(bucket_locks[bucket_idx].rw_lock);

        int32_t fd_idx = aether_open(idx_path, FileMode::READ);
        aether_seek(fd_idx, sizeof(IndexHeader) + bucket_idx * sizeof(IndexRecord));
        IndexRecord record;
        aether_read(fd_idx, reinterpret_cast<uint8_t *>(&record), sizeof(IndexRecord));
        aether_close(fd_idx);

        if (record.key_len == 0 && record.data_offset == TOMBSTONE_OFFSET)
        {
            return response; // Encuentra un espacio virgen, la llave no existe
        }

        if (record.key_len == key.length() && std::strncmp(record.key, key.c_str(), key.length()) == 0)
        {
            if (record.data_offset == TOMBSTONE_OFFSET)
                return response; // Existe, pero fue borrada lógicamente

            // Encontrada: Lee el payload del archivo de datos
            int32_t fd_dat = aether_open(dat_path, FileMode::READ);
            aether_seek(fd_dat, record.data_offset);

            DataRecordHeader d_header;
            aether_read(fd_dat, reinterpret_cast<uint8_t *>(&d_header), sizeof(DataRecordHeader));

            uint32_t read_len = std::min(d_header.payload_len, MAX_PAYLOAD_SIZE - 1);
            aether_read(fd_dat, reinterpret_cast<uint8_t *>(response.payload), read_len);
            response.payload[read_len] = '\0';

            response.payload_len = read_len;
            response.status = DBStatus::OK;
            aether_close(fd_dat);
            return response;
        }
    }
    return response;
}

DBResponse AetherDatabase::internal_remove(std::string key)
{
    DBResponse response;
    response.status = DBStatus::NOT_FOUND;
    uint32_t hash = calculate_hash(key);

    for (uint32_t i = 0; i < HASH_TABLE_BUCKETS; ++i)
    {
        uint32_t bucket_idx = (hash + i) % HASH_TABLE_BUCKETS;
        std::unique_lock<std::shared_mutex> lock(bucket_locks[bucket_idx].rw_lock);

        int32_t fd_idx = aether_open(idx_path, FileMode::READ);
        aether_seek(fd_idx, sizeof(IndexHeader) + bucket_idx * sizeof(IndexRecord));
        IndexRecord record;
        aether_read(fd_idx, reinterpret_cast<uint8_t *>(&record), sizeof(IndexRecord));
        aether_close(fd_idx);

        if (record.key_len == 0 && record.data_offset == TOMBSTONE_OFFSET)
            return response;

        if (record.key_len == key.length() && std::strncmp(record.key, key.c_str(), key.length()) == 0)
        {
            if (record.data_offset != TOMBSTONE_OFFSET)
            {
                record.data_offset = TOMBSTONE_OFFSET; // Borrado Lógico: Marcado como Tombstone

                int32_t fd_idx_write = aether_open(idx_path, FileMode::WRITE);
                aether_seek(fd_idx_write, sizeof(IndexHeader) + bucket_idx * sizeof(IndexRecord));
                aether_write(fd_idx_write, reinterpret_cast<const uint8_t *>(&record), sizeof(IndexRecord));
                aether_close(fd_idx_write);

                response.status = DBStatus::OK;
            }
            return response;
        }
    }
    return response;
}

// ==========================================
// WRAPPERS ASÍNCRONOS PÚBLICOS (Encolado al ThreadPool)
// ==========================================

std::future<DBResponse> AetherDatabase::store(const std::string &key, const std::string &payload)
{
    return thread_pool->submit_task([this, key, payload]()
                                    { return internal_store(key, payload); });
}

std::future<DBResponse> AetherDatabase::fetch(const std::string &key)
{
    return thread_pool->submit_task([this, key]()
                                    { return internal_fetch(key); });
}

std::future<DBResponse> AetherDatabase::remove(const std::string &key)
{
    return thread_pool->submit_task([this, key]()
                                    { return internal_remove(key); });
}