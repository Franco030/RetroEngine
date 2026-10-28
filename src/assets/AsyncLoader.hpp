#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace retro {

// Hilos de trabajo para lo que no toca la GPU: leer y parsear archivos (OBJ, PNG, JSON). Cada
// tarea devuelve un std::future; el hilo principal lo recoge cuando esta listo y hace la parte de
// GPU (subir la malla o la textura), que es rapida. Las tareas no deben llamar a nada de OpenGL.
class AsyncLoader {
public:
  explicit AsyncLoader(std::size_t threads) {
    for (std::size_t i = 0; i < threads; ++i)
      workers_.emplace_back([this] { Work(); });
  }

  ~AsyncLoader() {
    {
      std::lock_guard lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : workers_)
      t.join(); // las tareas que quedan en la cola se descartan (sus futuros dan broken_promise)
  }

  AsyncLoader(const AsyncLoader&) = delete;
  AsyncLoader& operator=(const AsyncLoader&) = delete;

  // Encola 'job' y devuelve su resultado futuro (o la excepcion que lance).
  template <class F> auto Submit(F job) -> std::future<std::invoke_result_t<F>> {
    using R = std::invoke_result_t<F>;
    // packaged_task no se puede copiar y std::function lo exige: va en un shared_ptr.
    auto task = std::make_shared<std::packaged_task<R()>>(std::move(job));
    std::future<R> result = task->get_future();
    {
      std::lock_guard lock(mutex_);
      queue_.emplace_back([task] { (*task)(); });
    }
    wake_.notify_one();
    return result;
  }

private:
  void Work() {
    while (true) {
      std::function<void()> job;
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_)
          return;
        job = std::move(queue_.front());
        queue_.pop_front();
      }
      job();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> queue_;
  bool stop_ = false;
  std::vector<std::thread> workers_; // al final: se crean cuando lo demas ya existe
};

} // namespace retro
