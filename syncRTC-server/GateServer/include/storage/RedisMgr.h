#pragma once

#include "common/global.h"
#include "common/Singleton.h"

#include <iostream>
#include <mutex>
#include <memory>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <sw/redis++/redis++.h>


class RedisConPool {
public:
    RedisConPool(std::size_t size, const char* host, int port, const char* pwd)
        : m_size(size), 
        m_stop(false),
        m_host(host ? host : ""), 
        m_port(port), 
        password(pwd ? pwd : ""), 
        create_size(0)
    {
        for(std::size_t i = 0; i < m_size; i++) {
            try {
                // 构建连接字符串
                // redis-plus-plus 要求连接串必须要带协议scheme
                std::string conn_str;
                if (m_host.find("://") == std::string::npos) {
                    conn_str = "tcp://" + m_host + ":" + std::to_string(m_port);
                } 
                else {
                    conn_str = m_host;
                    const bool is_unix = (conn_str.rfind("unix://", 0) == 0);
                    if (!is_unix) {
                        const auto scheme_pos = conn_str.find("://");
                        const auto host_pos = (scheme_pos == std::string::npos) ? 0 : (scheme_pos + 3);
                        if (conn_str.find(':', host_pos) == std::string::npos) {
                            conn_str += ":" + std::to_string(m_port);
                        }
                    }
                }

                // 创建 redis-plus-plus 客户端（替代 redisContext）
                auto conn = std::make_shared<sw::redis::Redis>(conn_str);

                // 认证（redis-plus-plus 已自动处理，这里显式认证兼容你的逻辑）
                if (!password.empty()) {
                    conn->auth(password);
                    std::cout << "Redis 认证成功" << std::endl;
                }

                std::string pong = conn->ping();
                if (pong != "PONG") {
                    throw std::runtime_error("ping failed");
                }

                m_buffer.push(conn);
                create_size++;
            }
            catch(const std::exception& e) {
                std::cerr << "创建 Redis 连接失败: " << e.what() << std::endl;
                continue;
            }
        }
    }

    ~RedisConPool() {
        Close();
        std::lock_guard<std::mutex> lock(m_mutex);
        while(!m_buffer.empty()) {
            m_buffer.pop();
        }
    }

    std::shared_ptr<sw::redis::Redis> getConnection() {
        std::unique_lock<std::mutex> m_lock(m_mutex);
        m_cond.wait(m_lock, [this](){
            if(m_stop) return true;
            return !m_buffer.empty();
        });

        if(m_stop) return nullptr;

        auto con = m_buffer.front();
        m_buffer.pop();
        return con;
    }

    void returnConnection(std::shared_ptr<sw::redis::Redis> connection) {
        if(!connection) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stop) return;
        m_buffer.push(std::move(connection));
        m_cond.notify_one();
    }

    void Close() {
        m_stop = true;
        m_cond.notify_all();
    }

    bool Healthy() {
        return !m_buffer.empty() && create_size.load() > 0;
    }

private:
    std::size_t m_size;
    std::atomic<bool> m_stop;
    std::string m_host;
    int m_port;
    std::string password;
    std::queue<std::shared_ptr<sw::redis::Redis>> m_buffer;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::atomic<std::size_t> create_size;
};



class RedisMgr : public Singleton<RedisMgr>, 
                public std::enable_shared_from_this<RedisMgr>
{
    friend class Singleton<RedisMgr>;
public:
    ~RedisMgr();

    // 连接 Redis（支持无密码/有密码）
    bool Connect(const std::string& host, int port, const std::string& password = "");
    
    // 基础 KV 操作
    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value, int expire_seconds = 0); // 可选过期时间
    
    // 认证（单独调用，也可在 Connect 里自动调用）
    bool Auth(const std::string& password);
    
    // List 操作
    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    
    // Hash 操作（重载，支持 string/char*）
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
    std::string HGet(const std::string& key, const std::string& hkey);
    
    // 键操作
    bool Del(const std::string& key);
    bool ExistsKey(const std::string& key);
    
    // 关闭连接
    void Close();

    // 检查是否已连接
    bool IsConnected() const { return _con_pool && _con_pool->Healthy(); }


private:
    RedisMgr();

    // 连接状态标记
    bool _is_connected = false;

    std::unique_ptr<RedisConPool> _con_pool;
};