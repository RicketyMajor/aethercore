#pragma once
#include "db_structures.hpp"
#include "thread_pool.hpp"
#include <string>
#include <shared_mutex>
#include <memory>

// Candado alineado a 64 bytes para aislarlo en la Caché L1 del procesador
struct alignas(64) HashBucketLock
{
    std::shared_mutex rw_lock;
};

class AetherDatabase
{
public:
    AetherDatabase(const std::string &db_name, uint32_t num_threads);
    ~AetherDatabase();

    // Inicializa o formatea la DB en el VFS
    bool format_db();

    // Interfaz pública asíncrona (Retornan Futures)
    std::future<DBResponse> store(const std::string &key, const std::string &payload);
    std::future<DBResponse> fetch(const std::string &key);
    std::future<DBResponse> remove(const std::string &key);

    AetherThreadPool *get_pool() { return thread_pool.get(); }
    DBResponse store_sync(const std::string &key, const std::string &payload) { return internal_store(key, payload); }
    DBResponse fetch_sync(const std::string &key) { return internal_fetch(key); }
    DBResponse remove_sync(const std::string &key) { return internal_remove(key); }

private:
    std::string db_name;
    std::string idx_path;
    std::string dat_path;

    std::unique_ptr<AetherThreadPool> thread_pool;

    // Arreglo de candados en RAM para el Fine-Grained Locking
    HashBucketLock bucket_locks[HASH_TABLE_BUCKETS];

    // Funciones internas sincrónicas que ejecutarán los hilos trabajadores
    DBResponse internal_store(std::string key, std::string payload);
    DBResponse internal_fetch(std::string key);
    DBResponse internal_remove(std::string key);

    uint32_t calculate_hash(const std::string &key);
};