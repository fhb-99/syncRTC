#pragma once

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

// 上限包含正在执行的任务。业务线程只尝试提交，绝不等待池中出现空位。
class SlowPool
{
public:
    SlowPool(std::size_t threads, std::size_t capacity);
    ~SlowPool();
    bool Submit(std::function<std::function<void()>()> task);
    bool Reserve();
    void Release();
    void Join();
private:
    void Run();
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::function<std::function<void()>()>> m_tasks;
    std::vector<std::thread> m_threads;
    std::size_t m_pending = 0;
    std::size_t m_reserved = 0;
    const std::size_t m_capacity;
    bool m_stopping = false;
};

class SerialCommandQueue;

class BusinessScheduler
{
public:
    explicit BusinessScheduler(std::size_t threads = 4, std::size_t capacity = 2048);
    ~BusinessScheduler();
    void WaitIdle();
    boost::asio::thread_pool& Pool() { return m_pool; }
private:
    friend class SerialCommandQueue;
    bool Reserve(bool terminal, std::size_t bytes);
    void Release(std::size_t bytes);
    boost::asio::thread_pool m_pool;
    std::mutex m_mutex;
    std::condition_variable m_idle;
    std::size_t m_pending = 0;
    const std::size_t m_capacity;
    std::size_t m_bytes = 0;
};

class SlowPoolBusy : public std::runtime_error
{
public:
    SlowPoolBusy() : std::runtime_error("慢任务池已满，任务尚未执行") {}
};

// 每个会议一个实例：strand 保护短步骤，commands + active_op 保护跨等待的完整操作。
// 使用 C++17 stackful coroutine 保存局部变量；挂起的是操作，不是业务 worker。
class SerialCommandQueue : public std::enable_shared_from_this<SerialCommandQueue>
{
public:
    using Executor = boost::asio::strand<boost::asio::thread_pool::executor_type>;
    class Operation
    {
    public:
        ~Operation() { ReleaseCapacity(); }
        std::uint64_t Id() const { return m_id; }
        std::uint64_t Phase() const { return m_phase; }
        void Reserve(SlowPool& pool);
        void ReleaseCapacity();

        // 慢任务只能访问调用方为本次等待准备的局部输入/输出，不能访问会议容器。
        // 异常和拒绝也回到同一 strand；完成通知不排到 commands 的尾部。
        template<class Work>
        auto Await(SlowPool& pool, Work work) -> std::invoke_result_t<Work>
        {
            using Result = std::invoke_result_t<Work>;
            Reserve(pool);
            static_assert(!std::is_void_v<Result>, "慢任务必须返回一个结果");
            struct State { std::optional<Result> result; std::exception_ptr error; };
            auto state = std::make_shared<State>();
            Wait([&pool, work = std::move(work), state](std::function<void()> resume) mutable {
                if (!pool.Submit([work = std::move(work), state, resume]() mutable {
                    try { state->result.emplace(work()); }
                    catch (...) { state->error = std::current_exception(); }
                    return resume;
                })) {
                    state->error = std::make_exception_ptr(SlowPoolBusy());
                    resume();
                }
            });
            if (state->error) std::rethrow_exception(state->error);
            return std::move(*state->result);
        }

        // 也用于等待目标会议操作完成；initiate 必须最终调用 resume，允许同步拒绝。
        void Wait(std::function<void(std::function<void()>)> initiate);
    private:
        friend class SerialCommandQueue;
        Operation(SerialCommandQueue& queue, std::uint64_t id, boost::asio::yield_context yield, bool terminal)
            : m_queue(queue), m_id(id), m_yield(yield), m_terminal(terminal) {}
        SerialCommandQueue& m_queue;
        const std::uint64_t m_id;
        std::uint64_t m_phase = 0;
        boost::asio::yield_context m_yield;
        bool m_terminal;
        std::vector<SlowPool*> m_reserved_pools;
    };

    using Command = std::function<void(Operation&)>;
    explicit SerialCommandQueue(BusinessScheduler& scheduler, std::size_t capacity = 128);
    bool Submit(Command command, std::function<void()> done = {}, bool terminal = false, std::size_t bytes = 0);
    std::size_t Pending() const { return m_pending.load(); }
    // 只在 strand 内、操作结束后调用。用于安全回收无成员的会议或空闲连接入口。
    std::function<void()> on_idle;
private:
    void Pump();
    struct Entry { Command command; std::function<void()> done; bool terminal; std::size_t bytes; };
    BusinessScheduler& m_scheduler;
    Executor m_strand;
    std::mutex m_admission;
    std::atomic<std::size_t> m_pending{0};
    const std::size_t m_capacity;
    std::size_t m_bytes = 0;
    std::deque<Entry> m_commands;
    std::uint64_t m_next_op = 0;
    std::uint64_t m_active_op = 0;
    std::uint64_t m_phase = 0;
};
