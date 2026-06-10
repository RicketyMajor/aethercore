#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include "vfs_structures.hpp"
#include "vfs_core.hpp"
#include "vfs_allocator.hpp"
#include "vfs_hierarchy.hpp"
#include "vfs_file_io.hpp"
#include "thread_pool.hpp"
#include "db_engine.hpp"

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
    std::vector<uint8_t> write_buffer(ten_mb, 'X'); // Llenamos 10MB con el caracter 'X'

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
            {
                std::cout << "Uso: dbinit <nombre_db>\n";
            }
            else
            {
                AetherDatabase db(arg, 0); // 0 = autodetección de hilos
                db.format_db();

                // ls automático para ver el resultado
                std::string absolute_db_path = resolve_shell_path(cwd, arg);
                std::cout << "\nVerificando VFS...\n";
                aether_ls(absolute_db_path);
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