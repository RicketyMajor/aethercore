#pragma once
#include <string>

// Funciones de la API POSIX simulada
bool aether_mkdir(const std::string &path);
bool aether_touch(const std::string &path);
void aether_ls(const std::string &path);
// Busca un i-nodo por su ruta completa y retorna su ID (o -1 si no existe)
int32_t aether_find_inode_by_path(const std::string &path);