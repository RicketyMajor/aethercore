#pragma once
#include <string>
#include <cstdint>

enum class FileMode
{
    READ,
    WRITE,
    APPEND
};

int32_t aether_open(const std::string &path, FileMode mode);
void aether_close(int32_t fd);

// Retornan la cantidad de bytes leídos/escritos reales
int32_t aether_write(int32_t fd, const uint8_t *buffer, uint32_t bytes);
int32_t aether_read(int32_t fd, uint8_t *buffer, uint32_t bytes);

bool aether_rm(const std::string &path);