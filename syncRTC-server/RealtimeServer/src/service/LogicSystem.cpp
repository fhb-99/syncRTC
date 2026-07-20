#include "service/LogicSystem.h"

#include <iostream>
#include <utility>

LogicSystem::LogicSystem()
    : m_stop(false)
{
    initHandlers();
    work_thread = std::thread(&LogicSystem::DealMessage, this);
}

LogicSystem::~LogicSystem()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cond.notify_all();

    if (work_thread.joinable()) {
        work_thread.join();
    }
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> message)
{
    if (!message) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_msg_que.push(std::move(message));
    }
    m_cond.notify_one();
}

void LogicSystem::DealMessage()
{
    while (true) {
        std::shared_ptr<LogicNode> message;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this]() {
                return m_stop || !m_msg_que.empty();
            });

            if (m_stop && m_msg_que.empty()) {
                return;
            }

            message = std::move(m_msg_que.front());
            m_msg_que.pop();
        }

        const auto handler = maps.find(message->id);
        if (handler == maps.end()) {
            std::cerr << "未注册的业务消息，id=" << message->id << std::endl;
            continue;
        }

        // 具体业务回调后续在 initHandlers 中注册，回调可通过 session->Send 写入响应。
        handler->second(message->session, message->id, std::move(message->message));
    }
}

void LogicSystem::initHandlers()
{
    // 当前只搭建投递和回调框架，认证、会议等业务处理后续再在此注册。
}
