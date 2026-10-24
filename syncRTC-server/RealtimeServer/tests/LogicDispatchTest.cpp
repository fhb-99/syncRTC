#include "service/LogicSystem.h"
#include <future>
#include <iostream>
#include <json/json.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
void Expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::shared_ptr<LogicNode> Node(const std::shared_ptr<Session>& session, int id)
{
    auto node = std::make_shared<LogicNode>();
    node->session = session;
    node->id = static_cast<std::uint16_t>(id);
    node->message = "{}";
    return node;
}
}

// 复用既有 friend 入口注入可控慢步骤；路由、会议注册表和调度均使用生产实现。
struct MediaSignalTestAccess
{
    static void TestRoutingAndOrdering()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        auto user = std::make_shared<Session>(-1);
        auto peer = std::make_shared<Session>(-1);
        user->SetUserId(1);
        peer->SetUserId(2);
        peer->SetMeetingId(8);
        std::promise<void> entered, release, other_done;
        auto gate = release.get_future().share();
        std::vector<int> trace;
        std::atomic<bool> good{true};
        logic->maps[6001] = [&](auto& op, auto& room, auto session, auto, auto) {
            if (!(room.id == 0)) good = false;
            trace.push_back(1);
            op.Await(logic->m_storage_pool, [&] { entered.set_value(); gate.wait(); return true; });
            session->SetMeetingId(7);
            trace.push_back(2);
        };
        logic->maps[6002] = [&](auto&, auto& room, auto, auto, auto) {
            if (!(room.id == 7 && trace == std::vector<int>({1,2}))) good = false;
            trace.push_back(3);
        };
        logic->maps[6003] = [&](auto&, auto& room, auto session, auto, auto) {
            if (!(room.id == 7 && trace == std::vector<int>({1,2,3}))) good = false;
            session->SetMeetingId(0);
            trace.push_back(4);
        };
        logic->maps[6004] = [&](auto&, auto& room, auto, auto, auto) {
            if (!(room.id == 8)) good = false;
            other_done.set_value();
        };
        logic->PostMsgToQue(Node(user, 6001));
        entered.get_future().wait();
        logic->PostMsgToQue(Node(user, 6002));
        logic->PostMsgToQue(Node(user, 6003));
        logic->PostMsgToQue(Node(peer, 6004));
        const bool other_progress = other_done.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        release.set_value();
        logic->m_scheduler.WaitIdle();
        Expect(other_progress && good && trace == std::vector<int>({1,2,3,4}), "等待时阻塞其他会议，或后续请求提前/错误路由");
        Expect(logic->m_connections.empty() && logic->m_rooms.empty(), "空闲连接或空会议未回收");
    }

    static void TestTwoConnectionsOneRoom()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        auto one = std::make_shared<Session>(-1);
        auto two = std::make_shared<Session>(-1);
        one->SetMeetingId(9);
        two->SetMeetingId(9);
        std::promise<void> entered, release;
        auto gate = release.get_future().share();
        std::atomic<int> active{0}, completed{0};
        std::atomic<bool> good{true};
        logic->maps[6005] = [&](auto& op, auto& room, auto, auto, auto) {
            if (!(++active == 1 && room.id == 9)) good = false;
            op.Await(logic->m_storage_pool, [&] { entered.set_value(); gate.wait(); return true; });
            --active;
            ++completed;
        };
        logic->maps[6006] = [&](auto&, auto& room, auto, auto, auto) {
            if (!(active == 0 && completed == 1 && room.id == 9)) good = false;
            ++completed;
        };
        logic->PostMsgToQue(Node(one, 6005));
        entered.get_future().wait();
        logic->PostMsgToQue(Node(two, 6006));
        release.set_value();
        logic->m_scheduler.WaitIdle();
        Expect(good && completed == 2, "同会议不同连接创建了并发操作");
    }

    static void TestRoomRegistryRetirementRace()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        std::atomic<int> active{0}, completed{0};
        std::atomic<bool> good{true};
        std::vector<std::thread> producers;
        for (int p = 0; p < 4; ++p) producers.emplace_back([&] {
            for (int i = 0; i < 128; ++i) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                while (!logic->SubmitRoom(88, [&](auto& op, auto&) {
                    if (++active != 1) good = false;
                    op.Wait([](auto resume) { resume(); });
                    if (active != 1) good = false;
                    --active;
                    ++completed;
                })) {
                    if (std::chrono::steady_clock::now() >= deadline) { good = false; return; }
                    std::this_thread::yield();
                }
                std::this_thread::yield();
            }
        });
        for (auto& producer : producers) producer.join();
        logic->m_scheduler.WaitIdle();
        Expect(good && completed == 512 && logic->m_rooms.empty(),
               "空闲回收与新提交竞争导致同会议双队列、丢任务或未回收");
    }

    static void TestUserReservation()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        std::promise<void> start;
        auto gate = start.get_future().share();
        auto a = std::async(std::launch::async, [&] { gate.wait(); return logic->ReserveUser(42, 1); });
        auto b = std::async(std::launch::async, [&] { gate.wait(); return logic->ReserveUser(42, 2); });
        start.set_value();
        const bool first = a.get(), second = b.get();
        Expect(first != second, "同一用户同时预留了两个会议");
        logic->FinishJoin(42, first ? 1 : 2, false);
        Expect(logic->ReserveUser(42, 3), "失败的入会未释放用户预留");
        logic->FinishJoin(42, 3, true);
        Expect(!logic->ReserveUser(42, 4), "已入会用户被另一会议覆盖");
        logic->ReleaseUser(42, 2);
        Expect(!logic->ReserveUser(42, 4), "旧会议清理覆盖了当前归属");
        logic->ReleaseUser(42, 3);
        Expect(logic->ReserveUser(42, 4), "离会未释放用户归属");
        logic->FinishJoin(42, 4, false);
    }

    static void TestDisconnectDuringSlowStep()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        auto session = std::make_shared<Session>(-1);
        std::promise<void> entered, release;
        auto gate = release.get_future().share();
        std::atomic<int> skipped{0};
        logic->maps[6007] = [&](auto& op, auto&, auto user, auto, auto) {
            op.Await(logic->m_storage_pool, [&] { entered.set_value(); gate.wait(); return true; });
            // 模拟慢步骤已经生效，不能因为 TCP 关闭就当作没有发生；随后断线命令负责收尾。
            user->SetMeetingId(9);
        };
        logic->maps[6008] = [&](auto&, auto&, auto, auto, auto) { ++skipped; };
        logic->PostMsgToQue(Node(session, 6007));
        entered.get_future().wait();
        logic->PostMsgToQue(Node(session, 6008));
        session->Close();
        logic->PostSessionDisconnected(session);
        release.set_value();
        logic->m_scheduler.WaitIdle();
        Expect(skipped == 0 && session->GetMeetingId() == 0 && logic->m_connections.empty(),
               "关闭后执行了排队请求或遗漏慢任务后的断线清理");
    }

    static void TestShutdownDrainsContinuation()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        auto session = std::make_shared<Session>(-1);
        std::promise<void> entered, release;
        auto gate = release.get_future().share();
        std::atomic<int> completed{0};
        logic->maps[6009] = [&](auto& op, auto&, auto, auto, auto) {
            op.Await(logic->m_storage_pool, [&] { entered.set_value(); gate.wait(); return true; });
            ++completed;
        };
        logic->PostMsgToQue(Node(session, 6009));
        entered.get_future().wait();
        auto stopping = std::async(std::launch::async, [&] { logic->Shutdown(); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        bool closed = false;
        while (!closed && std::chrono::steady_clock::now() < deadline) {
            { std::lock_guard<std::mutex> lock(logic->m_registry_mutex); closed = logic->m_stopping; }
            std::this_thread::yield();
        }
        const bool waited = stopping.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout;
        logic->PostMsgToQue(Node(session, 6009)); // 关闭入口后不能再接纳普通请求。
        release.set_value();
        stopping.get();
        logic->Shutdown(); // main 的显式关闭与析构兜底可以重复调用。
        Expect(closed && waited && completed == 1 && logic->m_connections.empty(),
               "关闭未等待完成回投，或停止入口后仍接纳新请求");
    }

    static void TestRealAiContinuation()
    {
        struct Service : message::MeetingAiService::Service {
            std::promise<void> entered, release;
            std::shared_future<void> gate = release.get_future().share();
            grpc::Status AskAssistant(grpc::ServerContext*, const message::AskAssistantRequest*,
                                      message::AskAssistantResponse* response) override {
                entered.set_value();
                gate.wait();
                response->set_answer("async answer");
                response->set_error(0);
                return grpc::Status::OK;
            }
        } service;
        grpc::ServerBuilder builder;
        int port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&service);
        auto server = builder.BuildAndStart();
        Expect(server && port > 0, "本地 AI stub 启动失败");
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        logic->m_ai_stub = message::MeetingAiService::NewStub(grpc::CreateChannel(
            "127.0.0.1:" + std::to_string(port), grpc::InsecureChannelCredentials()));
        int pair[2];
        Expect(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair) == 0, "AI 回包 socketpair 创建失败");
        auto session = std::make_shared<Session>(pair[0]);
        session->SetUserId(1);
        auto node = Node(session, ID_AI_ASK_REQUEST);
        node->message = R"({"question":"test"})";
        logic->PostMsgToQue(node);
        const bool entered = service.entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        std::promise<void> independent;
        auto peer = std::make_shared<Session>(-1);
        peer->SetMeetingId(10);
        // 注册表运行中不修改：用生产 SubmitRoom 提交一个不依赖 AI 的会议命令。
        logic->SubmitRoom(10, [&](auto&, auto&) { independent.set_value(); });
        const bool progress = independent.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        service.release.set_value();
        logic->m_scheduler.WaitIdle();
        Expect(session->HandleWrite(), "AI 响应发送失败");
        char buffer[1024];
        const auto count = ::recv(pair[1], buffer, sizeof(buffer), 0);
        Json::Value response;
        Json::Reader reader;
        const bool reply = count > 4 && reader.parse(std::string(buffer + 4, static_cast<std::size_t>(count) - 4), response) &&
            response["error"].asInt() == 0 && response["answer"].asString() == "async answer";
        session->Close();
        ::close(pair[0]);
        ::close(pair[1]);
        server->Shutdown();
        Expect(entered && progress && reply, "真实 AI RPC 未释放业务执行器或未正确续行回包");
    }
};

int main()
{
    try {
        MediaSignalTestAccess::TestRoutingAndOrdering();
        MediaSignalTestAccess::TestTwoConnectionsOneRoom();
        MediaSignalTestAccess::TestRoomRegistryRetirementRace();
        MediaSignalTestAccess::TestUserReservation();
        MediaSignalTestAccess::TestDisconnectDuringSlowStep();
        MediaSignalTestAccess::TestShutdownDrainsContinuation();
        MediaSignalTestAccess::TestRealAiContinuation();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
