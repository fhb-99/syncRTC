#include "storage/MySqlMgr.h"
#include <future>

int main()
{
    // 零连接模拟初始化全部失败后的实际池状态，不连接外部 MySQL。
    MysqlPool pool("", "", "", "", 0);
    // 多个新线程同时首次使用真实 Connector，覆盖驱动一次初始化及 threadInit/threadEnd。
    std::promise<void> start;
    auto gate = start.get_future().share();
    std::vector<std::future<std::unique_ptr<SqlConnection>>> borrowers;
    for (int i = 0; i < 4; ++i) borrowers.push_back(std::async(std::launch::async, [&] {
        gate.wait();
        return pool.getConnection();
    }));
    start.set_value();
    bool bounded = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (auto& borrowed : borrowers)
        if (borrowed.wait_until(deadline) != std::future_status::ready) bounded = false;
    pool.Close(); // 回归失败时也唤醒旧实现，避免测试自身挂死。
    bool unexpected_connection = false;
    for (auto& borrowed : borrowers) if (borrowed.get()) unexpected_connection = true;
    if (unexpected_connection || !bounded) {
        std::cerr << "空 MySQL 连接池无限等待，慢任务和 Shutdown 无法完成\n";
        return 1;
    }
    if (pool.getConnection()) return 1;
}
