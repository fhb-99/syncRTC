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
    bool GetHistoryMeeting(std::vector<HistoryMeetingInfo>& meetings);

private:
    friend class Singleton<MysqlMgr>;
    MysqlMgr();
    std::unique_ptr<MysqlPool> pool_;
};
