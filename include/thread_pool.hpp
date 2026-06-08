#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

class AetherThreadPool
{
public:
    AetherThreadPool(uint32_t num_threads);
    ~AetherThreadPool();

    // Encola una tarea y devuelve un future con su resultado
    template <class F, class... Args>
    auto submit_task(F &&f, Args &&...args) -> std::future<typename std::invoke_result<F, Args...>::type>;

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

// La implementación del template debe ir en el header
template <class F, class... Args>
auto AetherThreadPool::submit_task(F &&f, Args &&...args) -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop)
            throw std::runtime_error("No se pueden encolar tareas en un ThreadPool detenido.");

        tasks.emplace([task]()
                      { (*task)(); });
    }
    condition.notify_one();
    return res;
}