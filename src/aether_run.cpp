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

constexpr int STACK_SIZE = 1024 * 1024; // 1 MB de Stack para el contenedor
const char *ROOTFS_DIR = "/tmp/aether_rootfs";

// Helper nativo para pivot_root (glibc no provee un wrapper directo para esta syscall)
int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

// Configuración de la jaula del sistema de archivos
int setup_filesystem()
{
    // 1. Marcar el montaje raíz actual como privado para que los cambios no se propaguen al host
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo al hacer el montaje raiz privado.\n";
        return -1;
    }

    // 2. Bind mount del RootFS sobre sí mismo (Requisito estricto del kernel para pivot_root)
    if (mount(ROOTFS_DIR, ROOTFS_DIR, "bind", MS_BIND | MS_REC, NULL) == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo en bind mount del RootFS.\n";
        return -1;
    }

    // 3. Crear el subdirectorio para acoplar temporalmente la raíz vieja
    std::string old_root = std::string(ROOTFS_DIR) + "/oldroot";
    mkdir(old_root.c_str(), 0755);

    // 4. ¡La magia de Linux! Cambiar el punto de montaje raíz físicamente
    if (pivot_root(ROOTFS_DIR, old_root.c_str()) == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo pivot_root: " << strerror(errno) << "\n";
        return -1;
    }

    // 5. Mover el hilo de ejecución a la nueva raíz ("/")
    if (chdir("/") == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo chdir.\n";
        return -1;
    }

    // 6. Montar el pseudo-sistema de archivos /proc para que el kernel reporte procesos locales
    mkdir("/proc", 0755);
    if (mount("proc", "/proc", "proc", 0, NULL) == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo al montar /proc.\n";
        return -1;
    }

    // 7. Desmontar perezosamente (MNT_DETACH) la raíz vieja y eliminar la carpeta puente
    if (umount2("/oldroot", MNT_DETACH) == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo al desmontar /oldroot.\n";
        return -1;
    }
    rmdir("/oldroot");

    return 0;
}

// Punto de entrada del proceso aislado (El Contenedor)
int container_main(void *arg)
{
    std::cout << "[Contenedor] Inicializando entorno aislado...\n";

    // --- FASE 2: CONFINAMIENTO ---

    // 1. Aislar visualmente el nodo en la red local
    const char *hostname = "aether-node";
    if (sethostname(hostname, strlen(hostname)) == -1)
    {
        std::cerr << "[Contenedor WARNING] No se pudo cambiar el hostname.\n";
    }

    // 2. Ejecutar la secuencia de enjaulamiento
    if (setup_filesystem() == -1)
    {
        return EXIT_FAILURE;
    }

    // -----------------------------

    std::cout << "[Contenedor] PID interno: " << getpid() << " (Confirmacion de CLONE_NEWPID)\n";

    char **args = static_cast<char **>(arg);
    std::cout << "[Contenedor] Ejecutando demonio o comando: " << args[0] << "\n\n";

    if (execvp(args[0], args) == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo execvp: " << strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    std::cout << "\n=======================================\n";
    std::cout << "  Aether Container Engine [Lanzador]   \n";
    std::cout << "=======================================\n";

    if (argc < 2)
    {
        std::cerr << "Uso: sudo ./aether_run <comando> [args...]\n";
        return EXIT_FAILURE;
    }

    char *stack = static_cast<char *>(malloc(STACK_SIZE));
    if (!stack)
    {
        std::cerr << "[Lanzador ERROR] Fallo al reservar memoria para el Stack.\n";
        return EXIT_FAILURE;
    }
    char *stack_top = stack + STACK_SIZE;

    std::cout << "[Lanzador] Ejecutando syscall clone() para crear Namespaces...\n";

    int clone_flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD;

    pid_t child_pid = clone(container_main, stack_top, clone_flags, argv + 1);

    if (child_pid == -1)
    {
        std::cerr << "[Lanzador ERROR] Fallo la syscall clone(): " << strerror(errno) << "\n";
        free(stack);
        return EXIT_FAILURE;
    }

    std::cout << "[Lanzador] Contenedor creado exitosamente. PID en el anfitrion: " << child_pid << "\n";

    if (waitpid(child_pid, nullptr, 0) == -1)
    {
        std::cerr << "[Lanzador ERROR] Fallo en waitpid.\n";
    }

    std::cout << "\n[Lanzador] El contenedor finalizo su ejecucion. Limpiando y apagando.\n";
    free(stack);
    return EXIT_SUCCESS;
}