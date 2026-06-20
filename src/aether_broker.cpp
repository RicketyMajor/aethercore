#include "aether_broker.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <sys/eventfd.h>
#include "db_engine.hpp"
#include <cstdlib>
#include <string>
#include <stdexcept>
extern std::unique_ptr<AetherDatabase> aether_db;

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

    // 3. Obtener el puerto desde la variable de entorno AETHER_PORT o usar el defecto
    int port = TCP_PORT; // 8080 por defecto
    if (const char *env_port = std::getenv("AETHER_PORT"))
    {
        try
        {
            port = std::stoi(env_port);
        }
        catch (...)
        {
            std::cerr << "[Broker WARNING] Variable AETHER_PORT invalida. Usando puerto " << TCP_PORT << ".\n";
        }
    }

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port); // Convierte a Network Byte Order

    if (bind(tcp_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        return false;

    // 4. Activar O_NONBLOCK
    if (!set_non_blocking(tcp_server_fd))
        return false;

    // 5. Poner en modo escucha (Backlog de SOMAXCONN para soportar ráfagas de 10k conexiones)
    if (listen(tcp_server_fd, SOMAXCONN) < 0)
        return false;

    std::cout << "[Broker] Escuchando TCP en el puerto " << port << " (Modo No-Bloqueante).\n";
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

    // Inicializar eventfd en modo No-Bloqueante
    notify_fd = eventfd(0, EFD_NONBLOCK);
    if (notify_fd == -1)
        return false;

    std::memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = notify_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notify_fd, &event);

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

            if (current_fd == tcp_server_fd || current_fd == unix_server_fd)
            {
                handle_new_connection(current_fd);
            }
            else if (current_fd == notify_fd)
            {
                // El Thread Pool tocó la campana: Hay respuestas listas
                uint64_t u;
                read(notify_fd, &u, sizeof(uint64_t)); // Limpiar la campana

                std::lock_guard<std::mutex> lock(pending_writes_mutex);
                while (!fds_with_pending_writes.empty())
                {
                    int fd = fds_with_pending_writes.front();
                    fds_with_pending_writes.pop();

                    // Activa la vigilancia de EPOLLOUT para este cliente
                    struct epoll_event event;
                    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
                    event.data.fd = fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
                }
            }
            else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, nullptr);
                session_manager.remove_session(current_fd);
            }
            else if (events[i].events & EPOLLIN)
            {
                handle_client_data(current_fd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                handle_client_write(current_fd);
            }
        }
    }
}

// ==========================================
// LECTURA Y FRAMING DEL BYTE-STREAM
// ==========================================

void AetherBroker::handle_client_data(int client_fd)
{
    Session *session = session_manager.get_session(client_fd);
    if (!session)
        return;

    char temp_buffer[4096]; // Buffer temporal para leer del socket

    while (true)
    {
        // Leer en modo No-Bloqueante
        ssize_t bytes_read = recv(client_fd, temp_buffer, sizeof(temp_buffer), 0);

        if (bytes_read > 0)
        {
            // Añadir los nuevos bytes al buffer histórico de la sesión
            session->read_buffer.insert(session->read_buffer.end(), temp_buffer, temp_buffer + bytes_read);
        }
        else if (bytes_read == 0)
        {
            // El cliente cerró la conexión ordenadamente (Half-close)
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
            session_manager.remove_session(client_fd);
            break;
        }
        else
        { // bytes_read == -1
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Ya no hay más datos por leer en el buffer del kernel en este momento.
                // Procesa lo que haya acumulado.
                process_session_buffer(session);
                break;
            }
            else
            {
                std::cerr << "[Broker ERROR] Fallo al leer del FD " << client_fd << ".\n";
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                session_manager.remove_session(client_fd);
                break;
            }
        }
    }
}

void AetherBroker::process_session_buffer(Session *session)
{
    // Usa un bucle while porque el cliente pudo haber mandado múltiples paquetes de golpe (Pipelining)
    while (true)
    {
        // 1. Se tienen suficientes bytes para el AetherHeader (12 bytes)?
        if (session->read_buffer.size() < sizeof(AetherHeader))
        {
            break; // Faltan datos, esperamos al siguiente evento epoll
        }

        // 2. Leer el Header temporalmente
        AetherHeader header;
        std::memcpy(&header, session->read_buffer.data(), sizeof(AetherHeader));

        // 3. Calcular el tamaño total del paquete (Header + Key + Payload)
        // Ojo: En un diseño real robusto, validaría que el tamaño no exceda un límite máximo para evitar ataques OOM.
        uint32_t total_packet_size = sizeof(AetherHeader) + header.key_len + header.payload_len;

        // 4. ¿Se tiene el paquete completo en el buffer?
        if (session->read_buffer.size() < total_packet_size)
        {
            break; // Tiene el header, pero el resto de los datos aún vienen en camino por la red
        }

        // 5. ¡SE TIENE EL PAQUETE COMPLETO! se extrae.
        uint32_t opcode_int = static_cast<uint32_t>(header.opcode);

        // Extracción visual para logs (Para validación en esta Fase 3)
        std::string extracted_key(
            session->read_buffer.begin() + sizeof(AetherHeader),
            session->read_buffer.begin() + sizeof(AetherHeader) + header.key_len);

        std::string extracted_payload(
            session->read_buffer.begin() + sizeof(AetherHeader) + header.key_len,
            session->read_buffer.begin() + total_packet_size);

        std::cout << "[Broker] PAQUETE COMPLETADO (FD " << session->fd << "):\n"
                  << "  -> Opcode : " << opcode_int << "\n"
                  << "  -> Llave  : " << extracted_key << "\n"
                  << "  -> Payload: " << extracted_payload << "\n\n";

        // 6. Limpiar del buffer los bytes que ya procesamos para procesar el siguiente paquete
        session->read_buffer.erase(session->read_buffer.begin(), session->read_buffer.begin() + total_packet_size);

        if (!aether_db)
        {
            std::cerr << "[Broker] Base de datos no inicializada. Paquete ignorado.\n";
            continue;
        }

        // Copia los datos para evadir problemas de memoria cuando el hilo despierte
        int c_fd = session->fd;
        std::string key = extracted_key;
        std::string payload = extracted_payload;

        // Despacha asíncronamente al Motor Concurrente
        aether_db->get_pool()->submit_task([this, c_fd, opcode_int, key, payload]()
                                           {
            DBResponse db_res;
            if (opcode_int == 1) db_res = aether_db->store_sync(key, payload);
            else if (opcode_int == 2) db_res = aether_db->fetch_sync(key);
            else if (opcode_int == 3) db_res = aether_db->remove_sync(key);
            else { db_res.status = DBStatus::ERROR; db_res.payload_len = 0; }

            // Empaquetar la respuesta
            AetherHeader res_header;
            res_header.opcode = static_cast<AetherOpcode>(opcode_int);
            res_header.status = static_cast<int32_t>(db_res.status);
            res_header.key_len = 0;
            res_header.payload_len = db_res.payload_len;

            std::vector<uint8_t> packet(sizeof(AetherHeader) + db_res.payload_len);
            std::memcpy(packet.data(), &res_header, sizeof(AetherHeader));
            if (db_res.payload_len > 0) {
                std::memcpy(packet.data() + sizeof(AetherHeader), db_res.payload, db_res.payload_len);
            }

            // Devolver al Broker
            this->enqueue_response(c_fd, packet); });
    }
}

void AetherBroker::enqueue_response(int client_fd, const std::vector<uint8_t> &packet)
{
    Session *session = session_manager.get_session(client_fd);
    if (!session)
        return; // Cliente desconectado

    {
        std::lock_guard<std::mutex> lock(session->write_mutex);
        session->write_queue.push(packet);
    }
    {
        std::lock_guard<std::mutex> lock(pending_writes_mutex);
        fds_with_pending_writes.push(client_fd);
    }

    // Tocar la campana eventfd para despertar a epoll_wait
    uint64_t u = 1;
    write(notify_fd, &u, sizeof(uint64_t));
}

void AetherBroker::handle_client_write(int client_fd)
{
    Session *session = session_manager.get_session(client_fd);
    if (!session)
        return;

    std::lock_guard<std::mutex> lock(session->write_mutex);
    while (!session->write_queue.empty())
    {
        auto &packet = session->write_queue.front();
        ssize_t sent = send(client_fd, packet.data(), packet.size(), 0);

        if (sent == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // Buffer de red local lleno
            else
                break; // Error de socket
        }
        session->write_queue.pop();
    }

    if (session->write_queue.empty())
    {
        // Silenciar EPOLLOUT para no consumir CPU en bucle infinito
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = client_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
    }
}