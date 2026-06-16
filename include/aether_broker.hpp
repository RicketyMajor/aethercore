#pragma once

#include "session_manager.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <sys/epoll.h>

constexpr int MAX_EPOLL_EVENTS = 10000;

class AetherBroker
{
public:
    AetherBroker();
    ~AetherBroker();

    // Arranca los sockets de escucha
    bool initialize();
    void start(); // Inicia el hilo despachador
    void stop();  // Apaga el hilo de forma segura

    // Getters para que el epoll pueda vigilar estos descriptores
    int get_tcp_fd() const { return tcp_server_fd; }
    int get_unix_fd() const { return unix_server_fd; }

private:
    int tcp_server_fd;
    int unix_server_fd;
    int epoll_fd; // Descriptor del propio epoll
    SessionManager session_manager;

    std::thread dispatcher_thread;
    std::atomic<bool> is_running;

    // Funciones internas de configuración
    bool setup_tcp_socket();
    bool setup_unix_socket();
    bool set_non_blocking(int fd);

    // Funciones del Motor de Eventos (Reactor)
    void dispatcher_loop();
    void handle_new_connection(int server_fd);

    // --- NUEVAS FUNCIONES FASE 3 ---
    void handle_client_data(int client_fd);
    void process_session_buffer(Session *session);
};