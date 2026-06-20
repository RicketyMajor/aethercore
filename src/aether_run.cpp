#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <iostream>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>

constexpr int STACK_SIZE = 1024 * 1024; // 1 MB de Stack
const char *ROOTFS_DIR = "/tmp/aether_rootfs";

int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

// --- FASE 3: CGROUPS V2 ---
bool setup_cgroups(pid_t child_pid, const std::string &mem_limit)
{
    std::string cg_path = "/sys/fs/cgroup/aethercore";

    // Crear el grupo de control (Ignoramos error si ya existe)
    mkdir(cg_path.c_str(), 0755);

    // 1. Establecer el límite estricto de memoria
    std::ofstream mem_file(cg_path + "/memory.max");
    if (!mem_file.is_open())
    {
        std::cerr << "[Lanzador ERROR] Tu sistema no soporta Cgroups v2 o la ruta es invalida.\n";
        return false;
    }
    mem_file << mem_limit;
    mem_file.close();

    // 2. Asignar el contenedor (PID) a esta prisión de recursos
    std::ofstream procs_file(cg_path + "/cgroup.procs");
    if (!procs_file.is_open())
        return false;
    procs_file << child_pid;
    procs_file.close();

    std::cout << "[Lanzador] Cgroups v2 configurado. Limite de Memoria: " << mem_limit << "\n";
    return true;
}

int setup_filesystem()
{
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
        return -1;
    if (mount(ROOTFS_DIR, ROOTFS_DIR, "bind", MS_BIND | MS_REC, NULL) == -1)
        return -1;

    std::string old_root = std::string(ROOTFS_DIR) + "/oldroot";
    mkdir(old_root.c_str(), 0755);

    if (pivot_root(ROOTFS_DIR, old_root.c_str()) == -1)
        return -1;
    if (chdir("/") == -1)
        return -1;

    mkdir("/proc", 0755);
    if (mount("proc", "/proc", "proc", 0, NULL) == -1)
        return -1;

    if (umount2("/oldroot", MNT_DETACH) == -1)
        return -1;
    rmdir("/oldroot");

    return 0;
}

// --- FASE 3: EL SUPERVISOR PID 1 ---
int container_main(void *arg)
{
    char **args = static_cast<char **>(arg);
    const char *hostname = "aether-node";
    sethostname(hostname, strlen(hostname));

    if (setup_filesystem() == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo el confinamiento.\n";
        return EXIT_FAILURE;
    }

    std::cout << "[Contenedor PID 1] Sistema de archivos montado. Iniciando Supervisor...\n";

    pid_t app_pid = fork();

    if (app_pid < 0)
    {
        std::cerr << "[Contenedor ERROR] Fallo el fork del supervisor.\n";
        return EXIT_FAILURE;
    }

    if (app_pid == 0)
    {
        // --- PROCESO APLICACIÓN (La Base de Datos) ---
        // Rebaja de Privilegios: Abandonamos root (UID 0) y pasamos a un usuario estándar (ej. 1000)
        setgid(1000);
        setuid(1000);

        std::cout << "[AetherCore Daemon] Arrancando imagen binaria...\n";
        if (execvp(args[0], args) == -1)
        {
            std::cerr << "[Demonio ERROR] Fallo execvp: " << strerror(errno) << "\n";
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        // --- PROCESO SUPERVISOR (PID 1) ---
        int status;
        while (true)
        {
            // El segador de Zombies: Espera a que CUALQUIER proceso hijo termine
            pid_t reaped_pid = waitpid(-1, &status, 0);

            // Si el proceso que murió fue el servidor principal, apagamos el contenedor
            if (reaped_pid == app_pid)
            {
                std::cout << "\n[Contenedor PID 1] El demonio principal finalizo. Apagando nodo.\n";
                break;
            }
        }
    }

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    std::cout << "\n=======================================\n";
    std::cout << "  Aether Container Engine [Lanzador]   \n";
    std::cout << "=======================================\n";

    // Parseo de los argumentos (ej. --memory 512M -- ./aether_server)
    std::string mem_limit = "max"; // Sin límite por defecto
    int cmd_idx = 1;

    if (argc >= 4 && std::string(argv[1]) == "--memory")
    {
        mem_limit = argv[2];
        cmd_idx = (std::string(argv[3]) == "--") ? 4 : 3;
    }
    else if (argc < 2)
    {
        std::cerr << "Uso: sudo ./aether_run [--memory <limite>] -- <comando> [args...]\n";
        return EXIT_FAILURE;
    }

    char *stack = static_cast<char *>(malloc(STACK_SIZE));
    if (!stack)
        return EXIT_FAILURE;
    char *stack_top = stack + STACK_SIZE;

    int clone_flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD;
    pid_t child_pid = clone(container_main, stack_top, clone_flags, argv + cmd_idx);

    if (child_pid == -1)
    {
        std::cerr << "[Lanzador ERROR] Fallo la syscall clone().\n";
        free(stack);
        return EXIT_FAILURE;
    }

    std::cout << "[Lanzador] Nodo virtual instanciado (Host PID: " << child_pid << ").\n";

    // --- FASE 3: Aplicar límite de hardware ---
    if (mem_limit != "max")
    {
        setup_cgroups(child_pid, mem_limit);
    }

    if (waitpid(child_pid, nullptr, 0) == -1)
    {
        std::cerr << "[Lanzador ERROR] Fallo en waitpid.\n";
    }

    std::cout << "[Lanzador] Orquestacion finalizada. Limpiando recursos.\n";

    // Limpieza de Cgroups al terminar
    rmdir("/sys/fs/cgroup/aethercore");
    free(stack);
    return EXIT_SUCCESS;
}