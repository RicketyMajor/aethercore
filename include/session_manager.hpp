#pragma once

#include "network_structures.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>

class SessionManager
{
public:
    SessionManager() = default;
    ~SessionManager();

    void add_session(int fd);
    void remove_session(int fd);
    Session *get_session(int fd);

private:
    std::unordered_map<int, std::unique_ptr<Session>> sessions;
    std::mutex sm_mutex; // Protege el mapa ante conexiones/desconexiones concurrentes
};