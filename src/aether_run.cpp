#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <iostream>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

constexpr int STACK_SIZE = 1024 * 1024; // 1 MB de Stack para el contenedor

// Punto de entrada del proceso aislado (El Contenedor)
int container_main(void *arg)
{
    std::cout << "[Contenedor] Inicializando entorno aislado...\n";
    std::cout << "[Contenedor] PID interno: " << getpid() << " (Si es 1, el aislamiento CLONE_NEWPID funciona)\n";

    char **args = static_cast<char **>(arg);

    // En la Fase 2, aquí montaremos el RootFS con pivot_root y aplicaremos Cgroups.
    // Por ahora, en la Fase 1, solo ejecutaremos el comando que nos pasen.

    std::cout << "[Contenedor] Ejecutando: " << args[0] << "\n\n";
    if (execvp(args[0], args) == -1)
    {
        std::cerr << "[Contenedor ERROR] Fallo execvp: " << strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS; // Nunca se alcanzará si execvp tiene éxito
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

    // 1. Reservar memoria para la pila (Stack) del nuevo proceso.
    // En arquitecturas x86/ARM, el stack crece hacia abajo (de direcciones altas a bajas),
    // por lo que a clone() le debemos pasar un puntero al FINAL de este bloque.
    char *stack = static_cast<char *>(malloc(STACK_SIZE));
    if (!stack)
    {
        std::cerr << "[Lanzador ERROR] Fallo al reservar memoria para el Stack.\n";
        return EXIT_FAILURE;
    }
    char *stack_top = stack + STACK_SIZE;

    std::cout << "[Lanzador] Ejecutando syscall clone() para crear Namespaces...\n";

    // 2. Flags mágicos de Linux para orquestar el aislamiento:
    // CLONE_NEWPID: Nuevo árbol de PIDs. El proceso creerá que está solo en la máquina.
    // CLONE_NEWNS: Nuevo espacio de puntos de montaje (necesario para pivot_root después).
    // CLONE_NEWUTS: Nuevo espacio para Hostname (nombres de red aislados).
    // SIGCHLD: Obligatorio para que waitpid funcione correctamente al salir.
    int clone_flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD;

    // 3. Crear el contenedor
    pid_t child_pid = clone(container_main, stack_top, clone_flags, argv + 1);

    if (child_pid == -1)
    {
        std::cerr << "[Lanzador ERROR] Fallo la syscall clone(): " << strerror(errno) << "\n";
        free(stack);
        return EXIT_FAILURE;
    }

    std::cout << "[Lanzador] Contenedor creado exitosamente. PID en el anfitrion: " << child_pid << "\n";

    // 4. El supervisor (Lanzador) espera a que el contenedor termine su ejecución
    if (waitpid(child_pid, nullptr, 0) == -1)
    {
        std::cerr << "[Lanzador ERROR] Fallo en waitpid.\n";
    }

    std::cout << "\n[Lanzador] El contenedor finalizo su ejecucion. Limpiando y apagando.\n";
    free(stack);
    return EXIT_SUCCESS;
}