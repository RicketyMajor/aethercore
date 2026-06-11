#include "aether_broker.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

AetherBroker::AetherBroker() : tcp_server_fd(-1), unix_server_fd(-1) {}

AetherBroker::~AetherBroker()
{
    if (tcp_server_fd != -1)
        close(tcp_server_fd);
    if (unix_server_fd != -1)
    {
        close(unix_server_fd);
        unlink(UNIX_SOCKET_PATH); // Limpiar el archivo del socket UNIX al salir
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
    {
        return false;
    }

    // 3. Bind al puerto 8080 (Escucha en todas las interfaces de red)
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(TCP_PORT); // Convierte a Network Byte Order

    if (bind(tcp_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cerr << "[Broker ERROR] Fallo el bind del socket TCP en el puerto " << TCP_PORT << ".\n";
        return false;
    }

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
    {
        std::cerr << "[Broker ERROR] Fallo el bind del socket UNIX en " << UNIX_SOCKET_PATH << ".\n";
        return false;
    }

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

    if (!setup_tcp_socket())
    {
        std::cerr << "[FATAL] No se pudo inicializar la interfaz TCP.\n";
        return false;
    }

    if (!setup_unix_socket())
    {
        std::cerr << "[FATAL] No se pudo inicializar la interfaz UNIX.\n";
        return false;
    }

    return true;
}