#pragma once
#include <string>

// Funciones de la API POSIX simulada
bool aether_mkdir(const std::string &path);
bool aether_touch(const std::string &path);
void aether_ls(const std::string &path);