#pragma once

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// 按会议固定分配工作线程的任务池。
//
// 与普通线程池共用一个任务队列、空闲线程竞争任务不同，本线程池使用 meeting_id
// 选择固定 worker。同一会议的任务只会由同一线程串行执行，避免多个线程并发修改
// MediaRoom；每个 worker 再按会议轮转，防止某个繁忙会议长期占用该线程。
template <typename Task>
class RoomThreadPool
{
public:
    using TaskHandler = std::function<void(Task)>;

    RoomThreadPool(std::size_t worker_count, TaskHandler handler)
        : m_handler(std::move(handler))
    {
        assert(worker_count > 0);
        m_workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            m_workers.push_back(std::make_unique<Worker>());
        }

        for (const auto& worker : m_workers) {
            worker->thread = std::thread([this, worker_ptr = worker.get()]() {
                RunWorker(*worker_ptr);
            });
        }
    }

    ~RoomThreadPool()
    {
        for (const auto& worker : m_workers) {
            {
                std::lock_guard<std::mutex> lock(worker->mutex);
                worker->stopping = true;
            }
            worker->condition.notify_one();
        }

        // 与原 LogicSystem 一致：停止接收新任务后，先处理已经进入队列的任务，
        // 再等待工作线程退出，避免服务析构时静默丢失已接收的媒体信令。
        for (const auto& worker : m_workers) {
            if (worker->thread.joinable()) {
                worker->thread.join();
            }
        }
    }

    RoomThreadPool(const RoomThreadPool&) = delete;
    RoomThreadPool& operator=(const RoomThreadPool&) = delete;

    bool Post(std::uint64_t meeting_id, Task task)
    {
        Worker& worker = *m_workers[WorkerIndex(meeting_id)];
        {
            std::lock_guard<std::mutex> lock(worker.mutex);
            if (worker.stopping) {
                return false;
            }

            RoomQueue& room_queue = worker.room_queues[meeting_id];
            room_queue.tasks.push(std::move(task));
            if (!room_queue.scheduled) {
                room_queue.scheduled = true;
                worker.ready_rooms.push_back(meeting_id);
            }
        }
        worker.condition.notify_one();
        return true;
    }

    std::size_t WorkerIndex(std::uint64_t meeting_id) const
    {
        return std::hash<std::uint64_t>{}(meeting_id) % m_workers.size();
    }

private:
    struct RoomQueue
    {
        std::queue<Task> tasks;
        // true 表示该会议已经在 ready_rooms 中，或者当前有一个任务正在执行。
        // 生产者只追加任务，不重复放入 meeting_id，避免就绪队列被热点会议占满。
        bool scheduled = false;
    };

    struct Worker
    {
        std::mutex mutex;
        std::condition_variable condition;
        std::unordered_map<std::uint64_t, RoomQueue> room_queues;
        std::deque<std::uint64_t> ready_rooms;
        std::thread thread;
        bool stopping = false;
    };

    void RunWorker(Worker& worker)
    {
        while (true) {
            std::uint64_t meeting_id = 0;
            std::optional<Task> task;
            {
                std::unique_lock<std::mutex> lock(worker.mutex);
                worker.condition.wait(lock, [&worker]() {
                    return worker.stopping || !worker.ready_rooms.empty();
                });

                if (worker.stopping && worker.ready_rooms.empty()) {
                    return;
                }

                meeting_id = worker.ready_rooms.front();
                worker.ready_rooms.pop_front();

                RoomQueue& room_queue = worker.room_queues.at(meeting_id);
                task.emplace(std::move(room_queue.tasks.front()));
                room_queue.tasks.pop();
            }

            // 任务执行期间不持有 worker 队列锁。其他 I/O 或回调线程仍可继续投递，
            // 不同 worker 也可以并行处理各自负责的会议。
            m_handler(std::move(*task));

            {
                std::lock_guard<std::mutex> lock(worker.mutex);
                auto room_it = worker.room_queues.find(meeting_id);
                if (!room_it->second.tasks.empty()) {
                    // 每轮只处理一个任务，然后把仍有任务的会议放到队尾。
                    // 这样保持会议内 FIFO，同时让同 worker 的其他会议获得执行机会。
                    worker.ready_rooms.push_back(meeting_id);
                }
                else {
                    worker.room_queues.erase(room_it);
                }
            }
        }
    }

    TaskHandler m_handler;
    std::vector<std::unique_ptr<Worker>> m_workers;
};
