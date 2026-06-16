#pragma once

#include "session_manager.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <sys/epoll.h>
#include <queue>
#include <mutex>

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
    int epoll_fd;  // Descriptor del propio epoll
    int notify_fd; // Descriptor eventfd

    SessionManager session_manager;
    std::thread dispatcher_thread;
    std::atomic<bool> is_running;

    // Cola compartida para avisar qué clientes tienen respuestas listas
    std::queue<int> fds_with_pending_writes;
    std::mutex pending_writes_mutex;

    // Funciones internas de configuración
    bool setup_tcp_socket();
    bool setup_unix_socket();
    bool set_non_blocking(int fd);

    // Funciones del Motor de Eventos (Reactor)
    void dispatcher_loop();
    void handle_new_connection(int server_fd);
    void handle_client_data(int client_fd);
    void process_session_buffer(Session *session);

    void handle_client_write(int client_fd);
    void enqueue_response(int client_fd, const std::vector<uint8_t> &packet);
};