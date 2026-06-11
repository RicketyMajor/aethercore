#include "aether_broker.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

AetherBroker::AetherBroker() : tcp_server_fd(-1), unix_server_fd(-1), epoll_fd(-1), is_running(false) {}

AetherBroker::~AetherBroker()
{
    stop(); // Detiene el hilo antes de cerrar descriptores
    if (epoll_fd != -1)
        close(epoll_fd);
    if (tcp_server_fd != -1)
        close(tcp_server_fd);
    if (unix_server_fd != -1)
    {
        close(unix_server_fd);
        unlink(UNIX_SOCKET_PATH);
    }
    std::cout << "[Broker] Sockets de escucha cerrados limpiamente.\n";
}

bool AetherBroker::set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return false;

    // Se añade la bandera O_NONBLOCK conservando las banderas previas
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        return false;
    return true;
}

bool AetherBroker::setup_tcp_socket()
{
    // 1. Crear el socket TCP (IPv4, Stream)
    tcp_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_server_fd == -1)
        return false;

    // 2. Permitir reuso rápido del puerto local
    int opt = 1;
    if (setsockopt(tcp_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        return false;

    // 3. Bind al puerto 8080 (Escucha en todas las interfaces de red)
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(TCP_PORT); // Convierte a Network Byte Order

    if (bind(tcp_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        return false;

    // 4. Activar O_NONBLOCK
    if (!set_non_blocking(tcp_server_fd))
        return false;

    // 5. Poner en modo escucha (Backlog de SOMAXCONN para soportar ráfagas de 10k conexiones)
    if (listen(tcp_server_fd, SOMAXCONN) < 0)
        return false;

    std::cout << "[Broker] Escuchando TCP en el puerto " << TCP_PORT << " (Modo No-Bloqueante).\n";
    return true;
}

bool AetherBroker::setup_unix_socket()
{
    // 1. Limpiar rastro anterior
    unlink(UNIX_SOCKET_PATH);

    // 2. Crear socket UNIX (Inter-Process Communication local)
    unix_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_server_fd == -1)
        return false;

    // 3. Bind a la ruta del archivo
    struct sockaddr_un address;
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, UNIX_SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (bind(unix_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        return false;

    // 4. Activar O_NONBLOCK
    if (!set_non_blocking(unix_server_fd))
        return false;

    // 5. Poner en modo escucha
    if (listen(unix_server_fd, SOMAXCONN) < 0)
        return false;

    std::cout << "[Broker] Escuchando UNIX Domain Socket en " << UNIX_SOCKET_PATH << " (Modo No-Bloqueante).\n";
    return true;
}

bool AetherBroker::initialize()
{
    std::cout << "\n[!] Inicializando Aether Message Broker...\n";
    if (!setup_tcp_socket() || !setup_unix_socket())
        return false;

    // --- INICIALIZAR EPOLL ---
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        std::cerr << "[Broker ERROR] No se pudo crear la instancia epoll.\n";
        return false;
    }

    struct epoll_event event;

    // Registrar Socket TCP en epoll
    std::memset(&event, 0, sizeof(event));
    event.events = EPOLLIN; // Detectar cuando hay datos (nuevas conexiones)
    event.data.fd = tcp_server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_server_fd, &event);

    // Registrar Socket UNIX en epoll
    std::memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = unix_server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, unix_server_fd, &event);

    return true;
}

void AetherBroker::start()
{
    is_running = true;
    dispatcher_thread = std::thread(&AetherBroker::dispatcher_loop, this);
    std::cout << "[Broker] Dispatcher Thread (Reactor epoll) iniciado en background.\n";
}

void AetherBroker::stop()
{
    is_running = false;
    if (dispatcher_thread.joinable())
    {
        dispatcher_thread.join();
    }
}

// Atiende ráfagas de clientes conectándose simultáneamente
void AetherBroker::handle_new_connection(int server_fd)
{
    while (true)
    {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // No hay más conexiones pendientes en la cola
            }
            else
            {
                break; // Otro tipo de error
            }
        }

        set_non_blocking(client_fd);
        session_manager.add_session(client_fd);

        // Registrar al cliente en el epoll para vigilar cuando nos envíe peticiones
        struct epoll_event event;
        std::memset(&event, 0, sizeof(event));
        // EPOLLIN (para leer) | EPOLLRDHUP (para detectar si el cliente se desconecta repentinamente)
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = client_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
    }
}

// Bucle Infinito del Hilo Despachador
void AetherBroker::dispatcher_loop()
{
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (is_running)
    {
        // Bloquea el hilo eficientemente. Despierta si hay eventos o pasan 100ms
        int n = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, 100);

        if (n == -1)
        {
            if (errno == EINTR)
                continue; // Si fue interrumpido por el SO, sigue iterando
            std::cerr << "[Broker ERROR] epoll_wait fallo.\n";
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            int current_fd = events[i].data.fd;

            // 1. Un nuevo cliente intenta conectarse
            if (current_fd == tcp_server_fd || current_fd == unix_server_fd)
            {
                handle_new_connection(current_fd);
            }
            // 2. El cliente cerró la conexión o la red falló
            else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, nullptr);
                session_manager.remove_session(current_fd);
            }
            // 3. Un cliente nos acaba de enviar datos (Fase 3)
            else if (events[i].events & EPOLLIN)
            {
                // Por ahora no lo lee, pero en la Fase 3 aquí inyectaremos el parsing
            }
        }
    }
}