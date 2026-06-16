#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include <string>
#include <mutex>

constexpr int TCP_PORT = 8080;
constexpr const char *UNIX_SOCKET_PATH = "/tmp/aether.sock";

enum class AetherOpcode : uint32_t
{
    STORE = 1,
    FETCH = 2,
    REMOVE = 3,
    PING = 4
};

// Aether Wire Protocol Header (Tamaño estricto: 16 bytes)
struct __attribute__((packed)) AetherHeader
{
    AetherOpcode opcode;
    int32_t status; // 0 = OK, 1 = NOT_FOUND, etc.
    uint32_t key_len;
    uint32_t payload_len;
};

struct Session
{
    int fd;
    std::vector<uint8_t> read_buffer;

    std::queue<std::vector<uint8_t>> write_queue;
    std::mutex write_mutex; // Protege la cola cuando el Thread Pool deposita respuestas

    bool header_parsed = false;
    AetherHeader current_header;
};