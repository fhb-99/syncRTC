#include "service/RoomThreadPool.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        return false;
    }
    return true;
}

template <typename Value>
bool WaitForSize(std::condition_variable& condition,
                 std::mutex& mutex,
                 const std::vector<Value>& values,
                 std::size_t expected_size)
{
    std::unique_lock<std::mutex> lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds(2), [&values, expected_size]() {
        return values.size() == expected_size;
    });
}

bool TestRoomFifoAndRoundRobin()
{
    std::mutex result_mutex;
    std::condition_variable result_condition;
    std::vector<int> result;
    std::promise<void> blocker_started;
    std::promise<void> release_blocker;
    std::shared_future<void> release_future = release_blocker.get_future().share();

    RoomThreadPool<std::function<void()>> pool(1, [](std::function<void()> task) {
        task();
    });

    pool.Post(99, [&blocker_started, release_future]() {
        blocker_started.set_value();
        release_future.wait();
    });
    blocker_started.get_future().wait();

    const auto append = [&result_mutex, &result_condition, &result](int value) {
        return [&result_mutex, &result_condition, &result, value]() {
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                result.push_back(value);
            }
            result_condition.notify_one();
        };
    };

    pool.Post(1001, append(11));
    pool.Post(1001, append(12));
    pool.Post(1001, append(13));
    pool.Post(2001, append(21));
    pool.Post(2001, append(22));
    release_blocker.set_value();

    if (!Expect(WaitForSize(result_condition, result_mutex, result, 5),
                "等待 room worker 执行任务超时")) {
        return false;
    }

    const std::vector<int> expected{11, 21, 12, 22, 13};
    return Expect(result == expected, "会议内 FIFO 或会议间轮转顺序错误");
}

bool TestFixedWorkerAffinity()
{
    std::mutex result_mutex;
    std::condition_variable result_condition;
    std::vector<std::thread::id> room_threads;

    RoomThreadPool<std::function<void()>> pool(2, [](std::function<void()> task) {
        task();
    });

    const auto record_thread = [&result_mutex, &result_condition, &room_threads]() {
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            room_threads.push_back(std::this_thread::get_id());
        }
        result_condition.notify_one();
    };

    for (int index = 0; index < 8; ++index) {
        pool.Post(1001, record_thread);
    }

    if (!Expect(WaitForSize(result_condition, result_mutex, room_threads, 8),
                "等待固定 worker 测试任务超时")) {
        return false;
    }

    for (const auto& thread_id : room_threads) {
        if (!Expect(thread_id == room_threads.front(), "同一会议被多个线程执行")) {
            return false;
        }
    }
    return true;
}

bool TestDifferentWorkersRunInParallel()
{
    std::mutex state_mutex;
    std::condition_variable state_condition;
    int started_count = 0;
    std::promise<void> release_tasks;
    std::shared_future<void> release_future = release_tasks.get_future().share();

    RoomThreadPool<std::function<void()>> pool(2, [](std::function<void()> task) {
        task();
    });

    std::uint64_t first_room = 1;
    std::uint64_t second_room = 2;
    while (pool.WorkerIndex(first_room) == pool.WorkerIndex(second_room)) {
        ++second_room;
    }

    const auto wait_for_release = [&state_mutex, &state_condition, &started_count, release_future]() {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            ++started_count;
        }
        state_condition.notify_one();
        release_future.wait();
    };

    pool.Post(first_room, wait_for_release);
    pool.Post(second_room, wait_for_release);

    bool both_started = false;
    {
        std::unique_lock<std::mutex> lock(state_mutex);
        both_started = state_condition.wait_for(lock, std::chrono::seconds(2), [&started_count]() {
            return started_count == 2;
        });
    }
    release_tasks.set_value();
    return Expect(both_started, "不同 worker 没有并行执行任务");
}

bool TestDestructorDrainsAcceptedTasks()
{
    std::mutex result_mutex;
    std::vector<int> result;
    {
        RoomThreadPool<std::function<void()>> pool(2, [](std::function<void()> task) {
            task();
        });
        for (int index = 0; index < 20; ++index) {
            pool.Post(static_cast<std::uint64_t>(index % 3), [&result_mutex, &result]() {
                std::lock_guard<std::mutex> lock(result_mutex);
                result.push_back(1);
            });
        }
    }
    return Expect(result.size() == 20, "线程池析构时丢失了已接收任务");
}

} // namespace

int main()
{
    if (!TestRoomFifoAndRoundRobin() ||
        !TestFixedWorkerAffinity() ||
        !TestDifferentWorkersRunInParallel() ||
        !TestDestructorDrainsAcceptedTasks()) {
        return 1;
    }
    return 0;
}
