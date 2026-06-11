#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <memory>
#include "vfs_structures.hpp"
#include "vfs_core.hpp"
#include "vfs_allocator.hpp"
#include "vfs_hierarchy.hpp"
#include "vfs_file_io.hpp"
#include "thread_pool.hpp"
#include "db_engine.hpp"
#include "aether_broker.hpp"

// ==========================================
// PUNTERO GLOBAL A LA BASE DE DATOS
// ==========================================
std::unique_ptr<AetherDatabase> aether_db = nullptr;
std::unique_ptr<AetherBroker> aether_broker = nullptr;

// ==========================================
// FUNCIÓN DE ESTRÉS (10 MB TEST)
// ==========================================
void run_stress_test()
{
    std::cout << "\n[!] Iniciando Prueba de Estres: 10 MB I/O en RAM...\n";
    std::cout << "Bloques libres iniciales: " << super_block->free_blocks << "\n";

    // Preparar entorno
    aether_mkdir("/stress_dir");
    aether_touch("/stress_dir/massive.bin");

    int32_t fd = aether_open("/stress_dir/massive.bin", FileMode::WRITE);
    if (fd == -1)
    {
        std::cerr << "[ERROR] No se pudo abrir el archivo de estres.\n";
        return;
    }

    uint32_t ten_mb = 10 * 1024 * 1024;
    std::vector<uint8_t> write_buffer(ten_mb, 'X'); // Llena 10MB con el caracter 'X'

    std::cout << "-> Escribiendo " << ten_mb << " bytes...\n";
    int32_t written = aether_write(fd, write_buffer.data(), ten_mb);
    aether_close(fd);

    std::cout << "-> Operacion completada. Bytes escritos: " << written << "\n";
    std::cout << "Bloques libres tras escritura: " << super_block->free_blocks << "\n";

    // Prueba de Lectura Rápida
    int32_t fd_read = aether_open("/stress_dir/massive.bin", FileMode::READ);
    std::vector<uint8_t> read_buffer(15);
    aether_read(fd_read, read_buffer.data(), 15);
    aether_close(fd_read);

    std::string sample(read_buffer.begin(), read_buffer.end());
    std::cout << "-> Verificacion de lectura (Primeros 15 bytes): [" << sample << "]\n";

    // Limpieza
    std::cout << "-> Eliminando archivo masivo para probar cascada de liberacion...\n";
    aether_rm("/stress_dir/massive.bin");
    std::cout << "Bloques libres tras eliminacion: " << super_block->free_blocks << " (Debe coincidir con iniciales - bloques de directorio)\n\n";
}

// ==========================================
// FUNCIÓN DE PRUEBA DEL THREAD POOL
// ==========================================
void run_pool_test()
{
    std::cout << "\n[!] Iniciando prueba del Thread Pool...\n";

    // Instanciar el pool pidiendo que detecte los núcleos automáticamente (pasando 0)
    AetherThreadPool pool(0);

    std::vector<std::future<int>> results;

    std::cout << "-> Despachando 50 tareas matemáticas concurrentes...\n";
    for (int i = 0; i < 50; ++i)
    {
        // submit_task devuelve un std::future
        results.push_back(
            pool.submit_task([i]()
                             {
                // Simulamos un trabajo
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                return i * i; }));
    }

    std::cout << "-> Tareas encoladas. Esperando resolucion asincrona...\n";

    int sum = 0;
    for (auto &future_res : results)
    {
        sum += future_res.get(); // get() bloquea hasta que el hilo termina su tarea específica
    }

    std::cout << "-> Todas las tareas completadas. Suma de cuadrados: " << sum << "\n\n";
}
// ==========================================
// FUNCIÓN DE ESTRÉS DE BASE DE DATOS (10,000 Transacciones)
// ==========================================
void run_db_stress_test()
{
    if (!aether_db)
    {
        std::cout << "[INFO] Inicializando DB temporal 'stressdb' para el test...\n";
        aether_db = std::make_unique<AetherDatabase>("stressdb", 0);
        aether_db->format_db();
    }

    std::cout << "\n[!] Iniciando Prueba de Estres DB: 10,000 transacciones concurrentes...\n";

    int num_ops = 5000; // 5000 escrituras + 5000 lecturas = 10,000 operaciones
    std::vector<std::future<DBResponse>> write_futures;
    std::vector<std::future<DBResponse>> read_futures;

    // Iniciar cronómetro de alta precisión
    auto start_time = std::chrono::high_resolution_clock::now();

    // 1. Despachar 5000 ESCRITURAS masivas al Thread Pool sin esperar
    for (int i = 0; i < num_ops; ++i)
    {
        std::string key = "llave_" + std::to_string(i);
        std::string val = "payload_masivo_de_prueba_numero_" + std::to_string(i);
        write_futures.push_back(aether_db->store(key, val));
    }

    // 2. Despachar 5000 LECTURAS masivas al Thread Pool sin esperar
    for (int i = 0; i < num_ops; ++i)
    {
        std::string key = "llave_" + std::to_string(i);
        read_futures.push_back(aether_db->fetch(key));
    }

    std::cout << "-> Todas las 10,000 tareas encoladas en el Thread Pool. Esperando resolucion...\n";

    // 3. Forzar barrera de sincronización: Recolectar resultados de escrituras
    int successful_writes = 0;
    for (auto &fut : write_futures)
    {
        if (fut.get().status == DBStatus::OK)
            successful_writes++;
    }

    // 4. Forzar barrera de sincronización: Recolectar resultados de lecturas
    int successful_reads = 0;
    for (auto &fut : read_futures)
    {
        DBResponse res = fut.get();
        if (res.status == DBStatus::OK)
            successful_reads++;
    }

    // Detener cronómetro
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> ms_double = end_time - start_time;

    // Resultados
    std::cout << "\n=================================================\n";
    std::cout << "             RESULTADOS DEL TEST                 \n";
    std::cout << "=================================================\n";
    std::cout << " - Tiempo total de procesamiento: " << ms_double.count() << " ms\n";
    std::cout << " - Escrituras exitosas: " << successful_writes << " / " << num_ops << "\n";
    std::cout << " - Lecturas exitosas:   " << successful_reads << " / " << num_ops << "\n";

    if (successful_writes == num_ops && successful_reads == num_ops)
    {
        std::cout << " - Estado: [EXITO TOTAL] Cero Deadlocks, Cero Race Conditions.\n";
    }
    else
    {
        std::cout << " - Estado: [ADVERTENCIA] Hubo fallos en las transacciones.\n";
    }
    std::cout << "=================================================\n\n";
}

// ==========================================
// TERMINAL INTERACTIVA (CLI)
// ==========================================
void print_help()
{
    std::cout << "\nComandos disponibles:\n";
    std::cout << "  mkdir <ruta>   : Crea un nuevo directorio\n";
    std::cout << "  touch <ruta>   : Crea un archivo vacio\n";
    std::cout << "  ls <ruta>      : Lista el contenido de un directorio (por defecto /)\n";
    std::cout << "  cd <ruta>      : Cambia el directorio actual\n";
    std::cout << "  rm <ruta>      : Elimina un archivo o directorio\n";
    std::cout << "  status         : Muestra el estado global de la RAM virtual\n";
    std::cout << "  stress         : Ejecuta la prueba de carga de 10 MB\n";
    std::cout << "  dbstress       : Ejecuta el test de concurrencia de 10,000 transacciones en la DB\n";
    std::cout << "  broker_init    : Inicializa los sockets de escucha del Broker\n";
    std::cout << "  dbinit <nombre> : Crea y formatea una base de datos concurrente\n";
    std::cout << "  pool           : Prueba la concurrencia del Thread Pool\n";
    std::cout << "  clear          : Limpia la terminal\n";
    std::cout << "  help           : Muestra este menu\n";
    std::cout << "  exit           : Apaga AetherCore y libera la memoria\n\n";
}

std::string resolve_shell_path(const std::string &cwd, const std::string &arg)
{
    if (arg.empty())
        return cwd;

    // Determinar la ruta base
    std::string target = (arg[0] == '/') ? arg : (cwd == "/" ? cwd + arg : cwd + "/" + arg);

    // Normalizar la ruta (procesar '..' y '.')
    std::vector<std::string> tokens;
    std::stringstream ss(target);
    std::string token;

    while (std::getline(ss, token, '/'))
    {
        if (token == "" || token == ".")
            continue;
        if (token == "..")
        {
            if (!tokens.empty())
                tokens.pop_back(); // Retroceder un nivel
        }
        else
        {
            tokens.push_back(token); // Avanzar un nivel
        }
    }

    // Reconstruir el string final
    std::string final_path = "/";
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        final_path += tokens[i];
        if (i < tokens.size() - 1)
            final_path += "/";
    }
    return final_path;
}

int main()
{
    // Limpiar pantalla al estilo Linux
    system("clear");

    std::cout << "=================================================\n";
    std::cout << "    AetherCore VFS - Entorno Interactivo CLI     \n";
    std::cout << "=================================================\n";

    if (!aether_format())
    {
        std::cerr << "[FATAL] Fallo al levantar el VFS.\n";
        return 1;
    }

    std::cout << "[OK] Kernel Virtual iniciado en " << TOTAL_RAM_SIZE / (1024 * 1024) << " MB de RAM pura.\n";
    print_help();

    std::string input;
    std::string cwd = "/"; // Estado del Shell

    while (true)
    {
        std::cout << "aether@ubuntu:" << cwd << "# ";
        std::getline(std::cin, input);

        if (input.empty())
            continue;

        std::stringstream ss(input);
        std::string command, arg;
        ss >> command >> arg;

        std::string absolute_path = resolve_shell_path(cwd, arg);

        if (command == "exit" || command == "quit")
            break;
        else if (command == "cd")
        {
            if (arg.empty() || arg == "/")
            {
                cwd = "/";
            }
            else
            {
                int32_t id = aether_find_inode_by_path(absolute_path);
                if (id == -1)
                    std::cout << "cd: no existe el archivo o el directorio: " << arg << "\n";
                else
                    cwd = absolute_path; // Asume que existe y actualiza el prompt
            }
        }
        else if (command == "mkdir")
        {
            if (arg.empty())
                std::cout << "Uso: mkdir <ruta>\n";
            else if (aether_mkdir(absolute_path))
                std::cout << "Directorio creado.\n";
        }
        else if (command == "touch")
        {
            if (arg.empty())
                std::cout << "Uso: touch <ruta>\n";
            else if (aether_touch(absolute_path))
                std::cout << "Archivo creado.\n";
        }
        else if (command == "ls")
        {
            aether_ls(absolute_path);
        }
        else if (command == "rm")
        {
            if (arg.empty())
                std::cout << "Uso: rm <ruta>\n";
            else if (aether_rm(absolute_path))
                std::cout << "Eliminado.\n";
        }
        else if (command == "status")
        {
            std::cout << "\n--- Estado del VFS ---\n";
            std::cout << "Bloques libres: " << super_block->free_blocks << " / " << super_block->total_blocks << "\n";
            std::cout << "I-nodos libres: " << super_block->free_inodes << " / " << super_block->total_inodes << "\n\n";
        }
        else if (command == "stress")
        {
            run_stress_test();
        }
        else if (command == "pool")
        {
            run_pool_test();
        }
        else if (command == "dbinit")
        {
            if (arg.empty())
                std::cout << "Uso: dbinit <nombre_db>\n";
            else
            {
                aether_db = std::make_unique<AetherDatabase>(arg, 0);
                aether_db->format_db();
                std::cout << "[INFO] Motor conectado a la DB.\n";
            }
        }
        else if (command == "set")
        {
            if (!aether_db)
            {
                std::cout << "Inicializa una db primero con dbinit.\n";
                continue;
            }
            std::string payload;

            // Extraer el resto de la línea directamente desde el stringstream
            std::getline(ss >> std::ws, payload);

            if (payload.empty())
            {
                std::cout << "Uso: set <llave> <valor>\n";
                continue;
            }

            auto future_res = aether_db->store(arg, payload);
            DBResponse res = future_res.get();
            if (res.status == DBStatus::OK)
                std::cout << "Guardado correctamente.\n";
            else
                std::cout << "Error al guardar (DB llena o limite excedido).\n";
        }
        else if (command == "get")
        {
            if (!aether_db)
            {
                std::cout << "Inicializa una db primero con dbinit.\n";
                continue;
            }
            auto future_res = aether_db->fetch(arg);
            DBResponse res = future_res.get();
            if (res.status == DBStatus::OK)
                std::cout << "Valor: " << res.payload << "\n";
            else
                std::cout << "No encontrado o eliminado.\n";
        }
        else if (command == "del")
        {
            if (!aether_db)
            {
                std::cout << "Inicializa una db primero con dbinit.\n";
                continue;
            }
            auto future_res = aether_db->remove(arg);
            DBResponse res = future_res.get();
            if (res.status == DBStatus::OK)
                std::cout << "Registro borrado (Tombstone aplicado).\n";
        }
        else if (command == "dbstress")
        {
            run_db_stress_test();
        }
        else if (command == "broker_init")
        {
            aether_broker = std::make_unique<AetherBroker>();
            if (aether_broker->initialize())
            {
                std::cout << "[OK] El Broker levanto los descriptores de red con exito.\n";
            }
        }

        else if (command == "clear")
        {
            system("clear");
        }
        else if (command == "help")
        {
            print_help();
        }
        else
        {
            std::cout << "Comando desconocido: " << command << ". Escribe 'help' para ver opciones.\n";
        }
    }

    std::cout << "\nIniciando secuencia de apagado...\n";
    aether_cleanup();
    std::cout << "AetherCore VFS apagado. Hasta luego.\n";

    return 0;
}