#pragma once

#include "common/global.h"
#include "common/Singleton.h"
#include "common/data.h"

#include <iostream>
#include <chrono>
#include <mutex>
#include <memory>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <vector>

#include <mysql/mysql.h>
#include <mysql/mysql_time.h>
#include <mysql/my_command.h>
#include <mysql/my_compress.h>
#include <mysql/my_list.h>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>  // 定义 sql::PreparedStatement
#include <cppconn/resultset.h>            // 定义 sql::ResultSet
#include <cppconn/exception.h>
#include <mysql_driver.h>

class SqlConnection
{
public:
	SqlConnection(sql::Connection* con, int64_t lasttime) : _con(con), _last_oper_time(lasttime) {}
	std::unique_ptr<sql::Connection> _con;
	int64_t _last_oper_time;
};

class MysqlPool {
public:
    MysqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize)
        : url_(url),
        user_(user),
        pass_(pass),
        schema_(schema),
        poolSize_(poolSize),
        b_stop_(false)
    {
        try {
            for (int i = 0; i < poolSize_; ++i) {
                sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
                auto*  con = driver->connect(url_, user_, pass_);
                con->setSchema(schema_);
                // 获取当前时间戳
				auto currentTime = std::chrono::system_clock::now().time_since_epoch();
				// 将时间戳转换为秒
				long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
				pool_.push(std::make_unique<SqlConnection>(con, timestamp));
            }
        }
        catch (sql::SQLException& e) {
            // 处理异常
            std::cout << "mysql pool init failed" << std::endl;
            std::cout << "mysql url=" << url_ << " user=" << user_ << " schema=" << schema_ << std::endl;
            std::cout << "mysql error: " << e.what()
                      << " (code=" << e.getErrorCode()
                      << ", state=" << e.getSQLState() << ")" << std::endl;
        }
    }

    std::unique_ptr<SqlConnection> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !pool_.empty(); });
        if (b_stop_) {
            return nullptr;
        }
        std::unique_ptr<SqlConnection> con(std::move(pool_.front()));
        pool_.pop();

        // MySQL 可能会回收长时间空闲的连接，使用前先探活并按需重建。
        lock.unlock();
        bool is_valid = false;
        try {
            is_valid = con && con->_con && !con->_con->isClosed() && con->_con->isValid();
        }
        catch (sql::SQLException&) {
            is_valid = false;
        }

        if (!is_valid) {
            try {
                sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
                auto new_con = std::unique_ptr<sql::Connection>(driver->connect(url_, user_, pass_));
                new_con->setSchema(schema_);
                if (con) {
                    con->_con = std::move(new_con);
                } else {
                    con = std::make_unique<SqlConnection>(new_con.release(), 0);
                }
                std::cout << "mysql connection reconnected" << std::endl;
            }
            catch (sql::SQLException& e) {
                std::cerr << "mysql reconnect failed: " << e.what()
                          << " (code=" << e.getErrorCode()
                          << ", state=" << e.getSQLState() << ")" << std::endl;
                returnConnection(std::move(con));
                return nullptr;
            }
        }

        con->_last_oper_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return con;
    }

    void returnConnection(std::unique_ptr<SqlConnection> con) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!con || b_stop_) {
            return;
        }
        pool_.push(std::move(con));
        cond_.notify_one();
    }

    void Close() {
        b_stop_ = true;
        cond_.notify_all();
    }

    ~MysqlPool() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!pool_.empty()) {
            pool_.pop();
        }
    }

private:
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    int poolSize_;
    std::queue<std::unique_ptr<SqlConnection>> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_;
};

class MysqlMgr : public Singleton<MysqlMgr>
{
public:
    ~MysqlMgr();
    // Returns the new user id, 0 when email already exists, and -1 on failure.
    int RegisterUser(const std::string& name,
                     const std::string& email,
                     const std::string& password_hash);

    bool CheckPwd(const std::string& email, std::string& password_hash);
	bool UpdatePwd(const std::string& email, const std::string& newpwd);

    bool GetUserInfo(const std::string& email, UserInfo& user);

    bool GetMeetingRecently(int uid, std::vector<RecentMeetingInfo>& meetings);

    // 在同一事务内创建会议并登记创建者，成功后返回可直接展示的会议数据。
    bool CreateMeeting(const CreateMeetingInfo& create_info,
                       RecentMeetingInfo& meeting);

    // 得到历史会议信息
    bool GetHistoryMeeting(int uid, std::vector<HistoryMeetingInfo>& meetings);

    // 根据唯一的meeting_code来查找用户所要参加的会议存不存在，其中包括会议的基本信息和创建者信息，返回false表示会议不存在或数据库查询失败。
    bool GetMeetingInfoByCode(const std::string& meeting_code, MeetingInfo& meeting);
    bool GetMeetingInfoById(std::uint64_t meeting_id, MeetingInfo& meeting);
    bool GetUserDisplayNameById(int user_id, std::string& display_name);

    // 仅当创建者仍在 scheduled 状态下开始会议，started_at 记录实际点击开始时间。
    bool StartMeeting(std::uint64_t meeting_id, int user_id);
    // 仅当创建者结束进行中的会议，ended_at 记录实际点击结束时间。
    bool EndMeeting(std::uint64_t meeting_id, int user_id);

    // 密码只在服务端内部校验，不能通过业务响应返回给客户端。
    bool GetMeetingPasswordHash(std::uint64_t meeting_id, std::string& password_hash);

    // 入会成功后，保存用户与会议的持久关系，并记录首次入会时间。
    bool UpdateMeetingPart(const MeetingInfo& meeting, int uid);

    // 保存会议聊天消息。message_id 和 created_at 由 MySQL 生成后回填；
    // receiver_user_id 为空表示群聊，不为空表示私聊。
    bool SaveMeetingMessage(MeetingMessageInfo& message);

    // 查询群聊历史消息。before_message_id 为 0 表示取最新一页，否则取该消息之前的一页。
    bool GetMeetingGroupMessages(std::uint64_t meeting_id,
                                 std::uint64_t before_message_id,
                                 std::uint32_t limit,
                                 std::vector<MeetingMessageInfo>& messages);

    // 查询两个用户在同一会议中的私聊历史。用户只能看到自己与对方之间的消息。
    bool GetMeetingPrivateMessages(std::uint64_t meeting_id,
                                   int user_id,
                                   int peer_user_id,
                                   std::uint64_t before_message_id,
                                   std::uint32_t limit,
                                   std::vector<MeetingMessageInfo>& messages);

private:
    friend class Singleton<MysqlMgr>;
    MysqlMgr();
    std::unique_ptr<MysqlPool> pool_;
};
