#pragma once

#include <grpcpp/grpcpp.h>
#include <mutex>
#include <memory>
#include <atomic>
#include <queue>
#include <condition_variable>

#include "common/message.grpc.pb.h"
#include "common/Singleton.h"
#include "common/global.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVarifyRequest;
using message::GetVarifyResponse;
using message::VarifyService;

class RPConPool 
{
public:
    RPConPool(std::size_t size, std::string host, std::string port)
        : m_poolSize(size), m_host(host), 
        m_port(port), m_stop(false) 
    {
        for(std::size_t i = 0; i < m_poolSize; i++) {
            std::shared_ptr<Channel> channel = grpc::CreateChannel(m_host + ":" + m_port, 
                grpc::InsecureChannelCredentials());

            m_pools.push(VarifyService::NewStub(channel));
        }
    }

    ~RPConPool() 
    {
        close();
        std::lock_guard<std::mutex> m_lock(m_mutex);
        while(!m_pools.empty()) {
            m_pools.pop();
        }
    }

    std::unique_ptr<VarifyService::Stub> getConnection()
    {
        std::unique_lock<std::mutex> m_lock(m_mutex);
        m_cond.wait(m_lock, [this](){
            if(m_stop) {
                return true;
            }
            return !m_pools.empty();
        });

        if(m_stop) {
            return nullptr;
        }

        auto connection = std::move(m_pools.front());
        m_pools.pop();
        return connection;
    }

    void returnConnection(std::unique_ptr<VarifyService::Stub> connection)
    {
        std::lock_guard<std::mutex> m_lock(m_mutex);
        if(m_stop) {
            return;
        }

        m_pools.push(std::move(connection));
        m_cond.notify_one();
    }

    void close()
    {
        m_stop = true;
        m_cond.notify_all();
    }


private:
    std::size_t m_poolSize;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::string m_host;
    std::string m_port;
    std::atomic<bool> m_stop;
    std::queue<std::unique_ptr<VarifyService::Stub>> m_pools;
};


class VarifyGrpcClient : public Singleton<VarifyGrpcClient>
{
    friend class Singleton<VarifyGrpcClient>;
public:
    GetVarifyResponse GetCode(std::string email);
private:
    VarifyGrpcClient();

    std::unique_ptr<VarifyService::Stub> m_stub;
    std::unique_ptr<RPConPool> m_rpc;
};