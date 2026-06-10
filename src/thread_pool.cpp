#include "thread_pool.hpp"
#include <iostream>

AetherThreadPool::AetherThreadPool(uint32_t num_threads) : stop(false)
{
    uint32_t threads_to_create = num_threads;

    // Autodetección de hardware si se pasa 0
    if (threads_to_create == 0)
    {
        threads_to_create = std::thread::hardware_concurrency();
        if (threads_to_create == 0)
            threads_to_create = 4; // Fallback de seguridad
    }

    std::cout << "[INFO] AetherThreadPool iniciando con " << threads_to_create << " hilos de trabajo.\n";

    for (uint32_t i = 0; i < threads_to_create; ++i)
    {
        workers.emplace_back([this]
                             {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    
                    // El hilo duerme aquí hasta que haya una tarea o se detenga el pool
                    this->condition.wait(lock, [this] { 
                        return this->stop || !this->tasks.empty(); 
                    });

                    // Si se dio la señal de parada y la cola está vacía, el hilo muere en paz
                    if (this->stop && this->tasks.empty()) {
                        return; 
                    }

                    // Extraer la tarea de la cola
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                // Ejecutar la tarea FUERA del candado para no bloquear a otros hilos
                task(); 
            } });
    }
}

AetherThreadPool::~AetherThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true; // Levantar la bandera de parada
    }
    condition.notify_all(); // Despertar a todos los hilos dormidos

    // Esperar a que cada hilo termine su ejecución actual y se una al hilo principal
    for (std::thread &worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    std::cout << "[INFO] AetherThreadPool apagado limpiamente.\n";
}