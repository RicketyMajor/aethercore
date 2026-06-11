#include "session_manager.hpp"
#include <unistd.h>
#include <iostream>

SessionManager::~SessionManager()
{
    std::lock_guard<std::mutex> lock(sm_mutex);
    // Asegura de cerrar todos los sockets si el servidor se apaga abruptamente
    for (auto &pair : sessions)
    {
        close(pair.first);
    }
    sessions.clear();
}

void SessionManager::add_session(int fd)
{
    std::lock_guard<std::mutex> lock(sm_mutex);
    auto session = std::make_unique<Session>();
    session->fd = fd;
    sessions[fd] = std::move(session);
    std::cout << "[Broker] Nueva sesion registrada. FD: " << fd << "\n";
}

void SessionManager::remove_session(int fd)
{
    std::lock_guard<std::mutex> lock(sm_mutex);
    if (sessions.erase(fd) > 0)
    {
        close(fd); // Libera el socket a nivel de SO
        std::cout << "[Broker] Sesion cerrada y eliminada. FD: " << fd << "\n";
    }
}

Session *SessionManager::get_session(int fd)
{
    std::lock_guard<std::mutex> lock(sm_mutex);
    auto it = sessions.find(fd);
    if (it != sessions.end())
    {
        return it->second.get();
    }
    return nullptr;
}