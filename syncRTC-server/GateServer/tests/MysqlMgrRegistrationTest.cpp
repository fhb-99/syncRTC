#include "storage/MySqlMgr.h"
#include "config/ConfigMgr.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::unique_ptr<sql::Connection> ConnectToMysql()
{
    auto& config = ConfigMgr::Init();
    const std::string host = config["Mysql"]["Host"];
    const std::string port = config["Mysql"]["Port"];
    const std::string user = config["Mysql"]["User"];
    const std::string password = config["Mysql"]["Password"];
    const std::string database = config["Mysql"]["Database"];

    auto* driver = sql::mysql::get_mysql_driver_instance();
    auto connection = std::unique_ptr<sql::Connection>(
        driver->connect("tcp://" + host + ":" + port, user, password));
    connection->setSchema(database);
    return connection;
}

class TestUserCleanup
{
public:
    explicit TestUserCleanup(std::string email) : email_(std::move(email)) {}

    ~TestUserCleanup()
    {
        try {
            auto connection = ConnectToMysql();
            auto stmt = std::unique_ptr<sql::PreparedStatement>(
                connection->prepareStatement("DELETE FROM users WHERE email = ?"));
            stmt->setString(1, email_);
            stmt->executeUpdate();
        } catch (const sql::SQLException&) {
        }
    }

private:
    std::string email_;
};

} // namespace

int main()
{
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string username = "registration_test_" + suffix;
    const std::string email = username + "@example.test";
    TestUserCleanup cleanup(email);

    const int first = MysqlMgr::GetInstance()->RegisterUser(
        username, email, "test-password-hash");
    if (first <= 0) {
        std::cerr << "first registration did not return a user id" << std::endl;
        return 1;
    }

    UserInfo user;
    if (!MysqlMgr::GetInstance()->GetUserInfo(email, user) ||
        user.uid != first || user.username != username || user.email != email) {
        std::cerr << "registered user information was not returned correctly" << std::endl;
        return 1;
    }

    UserInfo user_by_uid;
    if (!MysqlMgr::GetInstance()->GetUserInfoByUid(first, user_by_uid) ||
        user_by_uid.uid != first || user_by_uid.username != username ||
        user_by_uid.email != email) {
        std::cerr << "registered user was not found by uid" << std::endl;
        return 1;
    }

    const int duplicate = MysqlMgr::GetInstance()->RegisterUser(
        username + "_second", email, "test-password-hash");
    if (duplicate != 0) {
        std::cerr << "duplicate email was not rejected" << std::endl;
        return 1;
    }

    return 0;
}
