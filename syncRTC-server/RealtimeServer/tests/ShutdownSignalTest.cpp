#include "net/CServer.h"
#include "service/LogicSystem.h"
#include <array>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
bool ReadMarker(int fd, char expected)
{
    pollfd item{fd, POLLIN, 0};
    int ready;
    do { ready = ::poll(&item, 1, 5000); } while (ready < 0 && errno == EINTR);
    char value = 0;
    return ready > 0 && ::read(fd, &value, 1) == 1 && value == expected;
}

void TestSignal(int signal)
{
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) throw std::runtime_error("创建退出测试管道失败");
    const auto child = ::fork();
    if (child < 0) throw std::runtime_error("创建退出测试子进程失败");
    if (child == 0) {
        ::close(pipe_fds[0]);
        try {
            // 必须早于业务池和 gRPC 线程创建；子线程继承此屏蔽集。
            sigset_t signals;
            ::sigemptyset(&signals);
            ::sigaddset(&signals, SIGTERM);
            ::sigaddset(&signals, SIGINT);
            if (::pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) _exit(2);
            auto logic = LogicSystem::GetInstance();
            {
                CServer server(0, logic->GetMediaSignalClient());
                server.Start("/tmp/syncrtc-signal-test-" + std::to_string(::getpid()) + ".sock");
                const char ready = 'R';
                ::write(pipe_fds[1], &ready, 1);
                server.Run();
            }
            logic->Shutdown();
            const char drained = 'D';
            ::write(pipe_fds[1], &drained, 1);
            _exit(0);
        } catch (...) { _exit(3); }
    }
    ::close(pipe_fds[1]);
    const bool ready = ReadMarker(pipe_fds[0], 'R');
    if (ready) ::kill(child, signal);
    const bool drained = ready && ReadMarker(pipe_fds[0], 'D');
    if (!drained) ::kill(child, SIGKILL); // 只回收本测试创建的子进程。
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    ::close(pipe_fds[0]);
    if (!drained || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("SIGTERM/SIGINT 未走 I/O 退出和业务排空路径");
}
}

int main()
{
    try { TestSignal(SIGTERM); TestSignal(SIGINT); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
