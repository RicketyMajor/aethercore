#pragma once
#include <cstdint>

// ==========================================
// ESTRUCTURAS DE RED (IPC / Pilar III)
// ==========================================
constexpr uint32_t MAX_PAYLOAD_SIZE = 4096;

enum class DBStatus : int32_t
{
    OK = 0,
    NOT_FOUND = 1,
    ALREADY_EXISTS = 2,
    ERROR = 3
};

// Esta es la estructura que viajará por Pipes/Sockets en el futuro
struct DBResponse
{
    DBStatus status;
    uint32_t payload_len;
    char payload[MAX_PAYLOAD_SIZE];
};

// ==========================================
// ESTRUCTURAS FÍSICAS DE LA BASE DE DATOS
// ==========================================
constexpr uint32_t HASH_TABLE_BUCKETS = 10007;    // Número primo para reducir colisiones
constexpr uint32_t TOMBSTONE_OFFSET = 0xFFFFFFFF; // Flag de borrado lógico
constexpr uint32_t MAX_KEY_LEN = 64;

// Representación de un registro de índice en el archivo (.idx)
struct IndexRecord
{
    uint32_t key_hash;
    uint32_t data_offset; // Puntero al archivo .dat, o TOMBSTONE_OFFSET si fue borrado
    uint32_t key_len;
    char key[MAX_KEY_LEN]; // Llave en crudo
};

// Cabecera del archivo (.idx)
struct IndexHeader
{
    uint32_t magic_number; // Para verificar que es un archivo DB válido
    uint32_t total_records;
};

// Representación de la cabecera de un registro de datos en el archivo (.dat)
struct DataRecordHeader
{
    uint32_t payload_len;
    // Inmediatamente después de esta cabecera irían los bytes crudos del payload
};