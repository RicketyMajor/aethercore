#pragma once

#include "session_manager.hpp"
#include <string>

class AetherBroker
{
public:
    AetherBroker();
    ~AetherBroker();

    // Arranca los sockets de escucha
    bool initialize();

    // Getters para que el epoll (Fase 2) pueda vigilar estos descriptores
    int get_tcp_fd() const { return tcp_server_fd; }
    int get_unix_fd() const { return unix_server_fd; }

private:
    int tcp_server_fd;
    int unix_server_fd;
    SessionManager session_manager;

    // Funciones internas de configuración
    bool setup_tcp_socket();
    bool setup_unix_socket();
    bool set_non_blocking(int fd);
};