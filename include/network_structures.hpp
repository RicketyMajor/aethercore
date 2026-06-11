#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include <string>

// Constantes de red
constexpr int TCP_PORT = 8080;
constexpr const char *UNIX_SOCKET_PATH = "/tmp/aether.sock";

// Opcodes para el Aether Wire Protocol
enum class AetherOpcode : uint32_t
{
    STORE = 1,
    FETCH = 2,
    REMOVE = 3,
    PING = 4
};

// Aether Wire Protocol Header (Tamaño estricto: 12 bytes)
struct __attribute__((packed)) AetherHeader
{
    AetherOpcode opcode;
    uint32_t key_len;
    uint32_t payload_len;
};

// Estructura de Sesión para cada cliente conectado
struct Session
{
    int fd; // File descriptor del socket del cliente

    // Buffer de entrada para ensamblar mensajes fragmentados por TCP
    std::vector<uint8_t> read_buffer;

    // Cola de salida para mensajes procesados y listos para enviarse al cliente
    std::queue<std::vector<uint8_t>> write_queue;

    // Estado de la lectura (Framing)
    bool header_parsed = false;
    AetherHeader current_header;
};