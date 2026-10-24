#include "service/BusinessScheduler.h"
#include <iostream>
#include <algorithm>

SlowPool::SlowPool(std::size_t threads, std::size_t capacity) : m_capacity(capacity)
{
    if (threads == 0 || capacity == 0) throw std::invalid_argument("线程数和容量必须大于零");
    for (std::size_t i = 0; i < threads; ++i) m_threads.emplace_back([this] { Run(); });
}
SlowPool::~SlowPool() { Join(); }
bool SlowPool::Reserve()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping || m_reserved >= m_capacity) return false;
    ++m_reserved;
    return true;
}
void SlowPool::Release()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    --m_reserved;
}
bool SlowPool::Submit(std::function<std::function<void()>()> task)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping || m_pending >= m_capacity) return false;
    m_tasks.push_back(std::move(task));
    ++m_pending;
    m_ready.notify_one();
    return true;
}
void SlowPool::Run()
{
    while (true) {
        std::function<std::function<void()>()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_ready.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
            if (m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }
        auto complete = task(); // Await 包装器捕获异常；先释放配额，再允许下一慢步骤提交。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_pending;
        }
        complete();
    }
}
void SlowPool::Join()
{
    { std::lock_guard<std::mutex> lock(m_mutex); m_stopping = true; }
    m_ready.notify_all();
    for (auto& thread : m_threads) if (thread.joinable()) thread.join();
}

BusinessScheduler::BusinessScheduler(std::size_t threads, std::size_t capacity)
    : m_pool(threads), m_capacity(capacity) {}
BusinessScheduler::~BusinessScheduler() { WaitIdle(); m_pool.join(); }
bool BusinessScheduler::Reserve(bool terminal, std::size_t bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if ((!terminal && m_pending >= m_capacity) || bytes > 64 * 1024 * 1024 - m_bytes) return false;
    ++m_pending;
    m_bytes += bytes;
    return true;
}
void BusinessScheduler::Release(std::size_t bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bytes -= bytes;
    if (--m_pending == 0) m_idle.notify_all();
}
void BusinessScheduler::WaitIdle()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle.wait(lock, [this] { return m_pending == 0; });
}

SerialCommandQueue::SerialCommandQueue(BusinessScheduler& scheduler, std::size_t capacity)
    : m_scheduler(scheduler), m_strand(boost::asio::make_strand(scheduler.Pool())), m_capacity(capacity) {}

bool SerialCommandQueue::Submit(Command command, std::function<void()> done, bool terminal, std::size_t bytes)
{
    // 接纳、计数和 post 在同一短锁下，确保同生产者顺序和关闭时的收尾机会。
    std::lock_guard<std::mutex> lock(m_admission);
    if ((!terminal && m_pending >= m_capacity) || bytes > 4 * 1024 * 1024 - m_bytes ||
        !m_scheduler.Reserve(terminal, bytes)) return false;
    ++m_pending;
    m_bytes += bytes;
    boost::asio::post(m_strand, [self = shared_from_this(), command = std::move(command), done = std::move(done), terminal, bytes]() mutable {
        self->m_commands.push_back({std::move(command), std::move(done), terminal, bytes});
        self->Pump();
    });
    return true;
}

void SerialCommandQueue::Pump()
{
    if (m_active_op || m_commands.empty()) return;
    const auto id = m_active_op = ++m_next_op;
    m_phase = 0;
    auto entry = std::move(m_commands.front());
    m_commands.pop_front();
    boost::asio::spawn(m_strand, [self = shared_from_this(), id, entry = std::move(entry)](boost::asio::yield_context yield) mutable {
        Operation operation(*self, id, yield, entry.terminal);
        try { entry.command(operation); }
        catch (const std::exception& error) { std::cerr << "业务操作异常：" << error.what() << std::endl; }
        operation.ReleaseCapacity();
        self->m_active_op = 0;
        if (entry.done) entry.done();
        {
            std::lock_guard<std::mutex> lock(self->m_admission);
            --self->m_pending;
            self->m_bytes -= entry.bytes;
        }
        // 不递归启动下一条命令，结果应用与响应提交完成后才重新排队 Pump。
        boost::asio::post(self->m_strand, [self, bytes = entry.bytes] {
            if (self->Pending() == 0 && self->on_idle) self->on_idle();
            self->Pump();
            self->m_scheduler.Release(bytes);
        });
    });
}

void SerialCommandQueue::Operation::Reserve(SlowPool& pool)
{
    if (std::find(m_reserved_pools.begin(), m_reserved_pools.end(), &pool) != m_reserved_pools.end()) return;
    while (!pool.Reserve()) {
        if (!m_terminal) throw SlowPoolBusy();
        // 断线收尾不能丢失。等待容量时仍挂起协程，不占业务线程，也不提前释放会议操作。
        boost::asio::steady_timer retry(m_queue.m_strand, std::chrono::milliseconds(10));
        retry.async_wait(m_yield);
    }
    m_reserved_pools.push_back(&pool);
}

void SerialCommandQueue::Operation::ReleaseCapacity()
{
    for (auto* pool : m_reserved_pools) pool->Release();
    m_reserved_pools.clear();
}

void SerialCommandQueue::Operation::Wait(std::function<void(std::function<void()>)> initiate)
{
    const auto phase = ++m_phase;
    m_queue.m_phase = phase;
    auto queue = m_queue.shared_from_this();
    // Boost 1.74 会移动非引用类型的 completion token；不能消耗后续阶段复用的上下文。
    auto yield = m_yield;
    boost::asio::async_initiate<boost::asio::yield_context, void()>(
        [queue, id = m_id, phase, initiate = std::move(initiate)](auto handler) mutable {
            auto completion = std::make_shared<decltype(handler)>(std::move(handler));
            auto delivered = std::make_shared<std::atomic<bool>>(false);
            initiate([queue, id, phase, completion, delivered] {
                if (delivered->exchange(true)) return;
                boost::asio::post(queue->m_strand, [queue, id, phase, completion] {
                    // 操作和阶段都匹配才允许恢复；任何完成都经过 strand，不在慢线程执行状态更新。
                    if (queue->m_active_op == id && queue->m_phase == phase) (*completion)();
                });
            });
        }, yield);
}
