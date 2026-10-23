#include "service/BusinessScheduler.h"
#include <array>
#include <future>
#include <iostream>

namespace {
void Expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void TestResumeAndOrder()
{
    // 只有一个业务 worker：A 等待慢任务时 B 仍必须完成，排除“用另一个业务线程等”的实现。
    BusinessScheduler scheduler(1);
    SlowPool storage(1, 1);
    auto a = std::make_shared<SerialCommandQueue>(scheduler, 3);
    auto b = std::make_shared<SerialCommandQueue>(scheduler);
    std::promise<void> entered, release, other_done;
    auto gate = release.get_future().share();
    std::vector<int> order;
    std::atomic<bool> good{true};
    const auto owner = std::this_thread::get_id();
    Expect(a->Submit([&](auto& op) {
        order.push_back(1);
        const auto business_thread = std::this_thread::get_id();
        const auto value = op.Await(storage, [&] {
            good = good && std::this_thread::get_id() != business_thread && std::this_thread::get_id() != owner;
            entered.set_value();
            gate.wait();
            return 7;
        });
        good = good && value == 7 && op.Phase() == 1 && std::this_thread::get_id() == business_thread;
        order.push_back(2);
        // 同一操作第二阶段复用已预留容量，不能在部分成功后因池满被打断。
        good = good && op.Await(storage, [] { return 8; }) == 8 && op.Phase() == 2;
        order.push_back(3);
    }), "A 首条命令拒绝");
    entered.get_future().wait();
    Expect(a->Submit([&](auto&) { good = good && order == std::vector<int>({1,2,3}); order.push_back(4); }), "聊天入队失败");
    Expect(a->Submit([&](auto&) { order.push_back(5); }), "离会入队失败");
    Expect(!a->Submit([](auto&) {}), "满会议队列未拒绝");
    Expect(b->Submit([&](auto&) { other_done.set_value(); }), "B 入队失败");
    const bool released_worker = other_done.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    release.set_value();
    scheduler.WaitIdle();
    Expect(released_worker, "等待慢任务占住了业务线程");
    Expect(good && order == std::vector<int>({1,2,3,4,5}), "慢任务结果未先恢复当前操作或执行位置错误");
}

void TestConcurrentProducers()
{
    BusinessScheduler scheduler(4, 4096);
    constexpr int rooms = 4, producers = 4, each = 64;
    std::array<std::shared_ptr<SerialCommandQueue>, rooms> queues;
    std::array<std::atomic<int>, rooms> inside{}, completed{};
    std::atomic<bool> concurrent{false};
    for (auto& queue : queues) queue = std::make_shared<SerialCommandQueue>(scheduler, 1024);
    std::vector<std::thread> senders;
    for (int p = 0; p < producers; ++p) senders.emplace_back([&, p] {
        for (int i = 0; i < each; ++i) for (int r = 0; r < rooms; ++r) {
            Expect(queues[r]->Submit([&, r, p, i](auto&) {
                if (++inside[r] != 1) concurrent = true;
                std::this_thread::yield();
                ++completed[r];
                --inside[r];
                (void)p; (void)i;
            }), "并发投递意外被拒绝");
        }
    });
    for (auto& thread : senders) thread.join();
    scheduler.WaitIdle();
    Expect(!concurrent, "同一会议发生并发执行");
    for (const auto& count : completed) Expect(count == producers * each, "休眠/唤醒竞争导致丢任务");
}

void TestRejectionExceptionsAndDuplicateCompletion()
{
    BusinessScheduler scheduler(2);
    SlowPool storage(1, 1);
    auto a = std::make_shared<SerialCommandQueue>(scheduler);
    auto b = std::make_shared<SerialCommandQueue>(scheduler);
    std::promise<void> entered, release, rejected;
    auto gate = release.get_future().share();
    std::atomic<int> value{0};
    a->Submit([&](auto& op) {
        op.Await(storage, [&] { entered.set_value(); gate.wait(); return true; });
        value += op.Await(storage, [] { return 1; });
    });
    entered.get_future().wait();
    b->Submit([&](auto& op) {
        try { op.Await(storage, [&] { value = -100; return false; }); }
        catch (const SlowPoolBusy&) { rejected.set_value(); }
    });
    const bool rejection = rejected.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    release.set_value();
    scheduler.WaitIdle();
    Expect(rejection && value == 1, "慢池拒绝未明确返回或执行了被拒绝的任务");
    b->Submit([&](auto& op) {
        try { op.Await(storage, []() -> int { throw std::runtime_error("storage failed"); }); }
        catch (const std::runtime_error&) { ++value; }
    });
    std::function<void()> old_resume;
    b->Submit([&](auto& op) {
        op.Wait([&](auto resume) { old_resume = resume; resume(); resume(); });
        ++value;
        op.Wait([&](auto resume) { old_resume(); resume(); });
        ++value;
    });
    b->Submit([&](auto&) { ++value; });
    scheduler.WaitIdle();
    old_resume();
    Expect(value == 5, "重复或旧阶段完成使操作重复应用/后续命令卡死");
    old_resume = {};
}

void TestDifferentRoomsParallel()
{
    BusinessScheduler scheduler(2);
    auto a = std::make_shared<SerialCommandQueue>(scheduler);
    auto b = std::make_shared<SerialCommandQueue>(scheduler);
    std::promise<void> a_entered, b_entered, release;
    auto gate = release.get_future().share();
    a->Submit([&](auto&) { a_entered.set_value(); gate.wait(); });
    b->Submit([&](auto&) { b_entered.set_value(); gate.wait(); });
    const bool parallel = a_entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready &&
        b_entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    release.set_value();
    scheduler.WaitIdle();
    Expect(parallel, "不同会议被全局业务锁串行化");
}

void TestCapacityAndTerminal()
{
    BusinessScheduler scheduler(1, 1);
    SlowPool storage(1, 1);
    auto queue = std::make_shared<SerialCommandQueue>(scheduler, 1);
    std::promise<std::function<void()>> paused;
    std::atomic<int> finished{0};
    Expect(queue->Submit([&](auto& op) {
        op.Wait([&](auto resume) { paused.set_value(resume); });
        ++finished;
    }, {}, false, 4 * 1024 * 1024), "允许的最大业务字节数未接纳");
    auto resume = paused.get_future().get();
    Expect(!queue->Submit([](auto&) {}, {}, false, 1), "在途操作未计入队列上限");
    Expect(queue->Submit([&](auto& op) {
        op.Await(storage, [] { return true; });
        ++finished;
    }, {}, true), "队列满时丢失断线收尾");
    resume();
    scheduler.WaitIdle();
    Expect(finished == 2, "满队列阻断了完成回调或收尾");
    Expect(queue->Submit([&](auto&) { ++finished; }, {}, false, 4 * 1024 * 1024), "完成后未归还队列配额");
    scheduler.WaitIdle();
    Expect(finished == 3, "队列未能再次调度");
}
}
int main()
{
    try {
        TestResumeAndOrder();
        TestConcurrentProducers();
        TestRejectionExceptionsAndDuplicateCompletion();
        TestDifferentRoomsParallel();
        TestCapacityAndTerminal();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
