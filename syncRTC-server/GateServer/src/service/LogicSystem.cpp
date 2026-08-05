#include "service/LogicSystem.h"
#include "rpc/VarifyGrpcClient.h"
#include "common/data.h"
#include "storage/RedisMgr.h"
#include "storage/MySqlMgr.h"
#include "config/ConfigMgr.h"

#include <iostream>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <crypt.h>
#include <cstdlib>
#include <array>
#include <iomanip>
#include <openssl/rand.h>
#include <sstream>



// 加盐哈希算法
std::string HashPassword(const std::string password)
{
    return LogicSystem::HashPassword(password);
}

bool VarifyPassword(const std::string& password, const std::string& stored_hash)
{
    return LogicSystem::VerifyPassword(password, stored_hash);
}

namespace {

constexpr unsigned long kBcryptCost = 12;
constexpr int kTokenExpireSeconds = 7 * 24 * 60 * 60;
constexpr char kSessionKeyPrefix[] = "auth::session:";

} // namespace

std::string LogicSystem::HashPassword(const std::string& password)
{
    char* setting = crypt_gensalt_ra("$2b$", kBcryptCost, nullptr, 0);
    if (!setting) {
        return {};
    }

    void* data = nullptr;
    int data_size = 0;
    char* hash = crypt_ra(password.c_str(), setting, &data, &data_size);
    std::string result = hash ? hash : "";

    std::free(setting);
    std::free(data);
    return result;
}

bool LogicSystem::VerifyPassword(const std::string& password,
                                 const std::string& stored_hash)
{
    if (stored_hash.empty()) {
        return false;
    }

    void* data = nullptr;
    int data_size = 0;
    char* hash = crypt_ra(password.c_str(), stored_hash.c_str(),
                          &data, &data_size);
    const bool matches = hash && stored_hash == hash;

    std::free(data);
    return matches;
}


std::string LogicSystem::GenerateToken()
{
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), bytes.size()) != 1) {
        return {};
    }

    std::ostringstream token;
    token << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        token << std::setw(2) << static_cast<int>(byte);
    }
    return token.str();
}

bool LogicSystem::SaveSession(const std::string& token, int uid,
                              const std::string& device_id)
{
    if (token.empty() || uid <= 0 || device_id.empty()) {
        return false;
    }

    Json::Value session;
    session["uid"] = uid;
    session["device_id"] = device_id;

    // 会话只保存用户和设备标识，Redis 的 TTL 即会话有效期。
    return RedisMgr::GetInstance()->Set(kSessionKeyPrefix + token,
                                        session.toStyledString(),
                                        kTokenExpireSeconds);
}

bool LogicSystem::ValidateSession(const std::string& token,
                                  const std::string& device_id, int& uid)
{
    uid = 0;
    if (token.empty() || device_id.empty()) {
        return false;
    }

    std::string session_value;
    if (!RedisMgr::GetInstance()->Get(kSessionKeyPrefix + token, session_value)) {
        return false;
    }

    Json::Value session;
    Json::Reader reader;
    if (!reader.parse(session_value, session) ||
        session["device_id"].asString() != device_id) {
        return false;
    }

    uid = session["uid"].asInt();
    return uid > 0;
}


bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con)
{
    if (_get_handlers.find(path) == _get_handlers.end())
    {
        return false;
    }

    _get_handlers[path](con);
    return true;
}

void LogicSystem::RegisterGet(std::string url, HttpHandler handler)
{
    _get_handlers.insert(make_pair(url, handler));
}

bool LogicSystem::HandlePost(std::string url, std::shared_ptr<HttpConnection> con)
{
    if (_post_handlers.find(url) == _post_handlers.end())
    {
        return false;
    }

    _post_handlers[url](con);
    return true;
}

void LogicSystem::RegisterPost(std::string url, HttpHandler handler)
{
    _post_handlers.insert(make_pair(url, handler));
}

LogicSystem::LogicSystem()
{
    //测试
    RegisterGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
        beast::ostream(connection->_response.body()) << "receive get_test request";
        int i = 0;
        for (auto& elem : connection->m_get_params)
        {
            i++;
            beast::ostream(connection->_response.body()) << "param" << i << " key is " << elem.first;
            beast::ostream(connection->_response.body()) << ", " <<  " value is " << elem.second << std::endl;
        }
    });

    RegisterPost("/get_varifycode", [](std::shared_ptr<HttpConnection> connection){
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;
        connection->_response.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success)
        {
            std::cout << "Failed to parse JSON data!" << std::endl;
            root["error"] = ErrorCodes::ERROR_JSON;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        auto email = src_root["email"].asString();
        GetVarifyResponse response = VarifyGrpcClient::GetInstance()->GetCode(email);
        std::cout << "email is " << email << std::endl;
        std::cout << "code is " << response.code();
        root["error"] = response.error();
        root["email"] = src_root["email"];
        root["code"] = response.code();
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        return true;
    });


    RegisterPost("/register_user", [](std::shared_ptr<HttpConnection> connection){
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;
        connection->_response.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool success = reader.parse(body_str, src_root);
        if(!success) {
            std::cout << "Failed to parse JSON data!" << std::endl;
            root["error"] = ErrorCodes::ERROR_JSON;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        UserData user_data;
        auto data = src_root["UserData"];
        user_data.username = data["username"].asString();
        user_data.email = data["email"].asString();
        user_data.password = data["password"].asString();
        std::string code = src_root["VarifyCode"].asString();
        std::string comfirm = src_root["Comfirm"].asString();

        if(comfirm != user_data.password) {
            std::cout << "password err " << std::endl;
            root["error"] = ErrorCodes::ERROR_PASSWORD;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        // redis中判断验证码是否一致
        std::string varify_code;
        bool b_get_code = RedisMgr::GetInstance()->Get("code_" + data["email"].asString(), varify_code);
        std::cout << "varify_code is: " << varify_code << std::endl;
        if(!b_get_code) {
            std::cout << "Get Varify Code Expired" << std::endl;
            root["error"] = ErrorCodes::ERROR_VARIFY_EXPIRED;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        if(varify_code != code) {
            std::cout << " varify code error" << std::endl;
            root["error"] = ErrorCodes::ERROR_VARIFYCODE;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        // 从数据库中查询用户
        std::string password_hash = HashPassword(user_data.password);
        int uid = MysqlMgr::GetInstance()->RegisterUser(user_data.username, user_data.email, password_hash);
        if(uid == -1) {
            std::cout << "MySql Error" << std::endl;
            root["error"] = ErrorCodes::ERROR_MYSQL;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }
        if(uid == 0) {
            std::cout << "User is Exist" << std::endl;
            root["error"] = ErrorCodes::ERROR_USER_EXIST;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        root["error"] = ErrorCodes::SUCCESS;
        root["email"] = user_data.email;
        root["uid"] = uid;
        root["username"] = user_data.username;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;

        return true;
    });

    RegisterPost("/login_user", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		// 请求体包含密码或会话令牌，不能写入日志。
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        const std::string device_id = src_root["device_id"].asString();
        if (device_id.empty()) {
            root["error"] = ErrorCodes::ERROR_JSON;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        if (src_root.isMember("session_token")) {
            // 会话恢复请求不携带账号和密码，只校验令牌所属设备。
            const std::string session_token = src_root["session_token"].asString();
            if (!RedisMgr::GetInstance()->IsConnected()) {
                root["error"] = ErrorCodes::ERROR_REDIS;
                std::string jsonstr = root.toStyledString();
                beast::ostream(connection->_response.body()) << jsonstr;
                return true;
            }

            int uid = 0;
            if (!LogicSystem::ValidateSession(session_token, device_id, uid)) {
                root["error"] = RedisMgr::GetInstance()->IsConnected()
                                    ? ErrorCodes::ERROR_SESSION_INVALID
                                    : ErrorCodes::ERROR_REDIS;
                std::string jsonstr = root.toStyledString();
                beast::ostream(connection->_response.body()) << jsonstr;
                return true;
            }

            UserInfo user_info;
            if (!MysqlMgr::GetInstance()->GetUserInfoByUid(uid, user_info)) {
                root["error"] = ErrorCodes::ERROR_MYSQL;
                std::string jsonstr = root.toStyledString();
                beast::ostream(connection->_response.body()) << jsonstr;
                return true;
            }

            // 校验通过后续期，token 不变。
            if (!LogicSystem::SaveSession(session_token, user_info.uid, device_id)) {
                root["error"] = ErrorCodes::ERROR_REDIS;
                std::string jsonstr = root.toStyledString();
                beast::ostream(connection->_response.body()) << jsonstr;
                return true;
            }

            auto& config = ConfigMgr::Init();
            root["error"] = ErrorCodes::SUCCESS;
            root["email"] = user_info.email;
            root["uid"] = user_info.uid;
            root["username"] = user_info.username;
            root["session_token"] = session_token;
            root["host"] = config["RealtimeServer"]["Host"];
            root["port"] = config["RealtimeServer"]["Port"];
            root["expires_in"] = kTokenExpireSeconds;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        const std::string email = src_root["account"].asString();
        const std::string password = src_root["password"].asString();
        if (email.empty() || password.empty()) {
            root["error"] = ErrorCodes::ERROR_JSON;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        // 检查密码是否匹配
        std::string password_hash = "";
        bool is_pwd = MysqlMgr::GetInstance()->CheckPwd(email, password_hash);
        if(!is_pwd || password_hash.empty()) {
            std::cout << "MYSQL ERROR" << std::endl;
			root["error"] = ErrorCodes::ERROR_MYSQL;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
        }
        bool is_match = VarifyPassword(password, password_hash);
        if(!is_match) {
            std::cout << " user pwd not match" << std::endl;
			root["error"] = ErrorCodes::ERROR_PASSWORD_INVALID;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
        }

        // 给客户端发送服务器的ip和port
        auto& config = ConfigMgr::Init();
        std::string host = config["RealtimeServer"]["Host"];
        std::string port = config["RealtimeServer"]["Port"];

        UserInfo user_info;
        // 数据库查询id和username
        bool is_user = MysqlMgr::GetInstance()->GetUserInfo(email, user_info);
        if(!is_user || user_info.uid <= 0 || user_info.username.empty()) {
            std::cout << "Get user info failed" << std::endl;
			root["error"] = ErrorCodes::ERROR_MYSQL;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
        }

        // 随机生成token， redis存储token， 设置过期时间
        const std::string session_token = LogicSystem::GenerateToken();
        if (session_token.empty() ||
            !LogicSystem::SaveSession(session_token, user_info.uid, device_id)) {
            std::cout << "Store token failed" << std::endl;
            root["error"] = ErrorCodes::ERROR_REDIS;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }


        root["error"] = ErrorCodes::SUCCESS;
        root["email"] = email;
        root["uid"] = user_info.uid;
        root["username"] = user_info.username;
        root["session_token"] = session_token;
        root["host"] = host;
        root["port"] = port;
        root["expires_in"] = kTokenExpireSeconds;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;

        return true;
    });


    RegisterPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection){
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        std::string email = src_root["email"].asString();
        std::string code = src_root["VarifyCode"].asString();
        std::string password = src_root["password"].asString();
        std::string confirm = src_root["confirm"].asString();

        if(confirm != password) {
            std::cout << "password err " << std::endl;
            root["error"] = ErrorCodes::ERROR_PASSWORD;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        // redis中判断验证码是否一致
        std::string varify_code;
        bool b_get_code = RedisMgr::GetInstance()->Get("code_" + src_root["email"].asString(), varify_code);
        if(!b_get_code) {
            std::cout << "Get Varify Code Expired" << std::endl;
            root["error"] = ErrorCodes::ERROR_VARIFY_EXPIRED;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        if(varify_code != code) {
            std::cout << " varify code error" << std::endl;
            root["error"] = ErrorCodes::ERROR_VARIFYCODE;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        // 更新密码
        std::string password_hash = HashPassword(password);
        bool is_update = MysqlMgr::GetInstance()->UpdatePwd(email, password_hash);
        if(!is_update) {
            std::cout << " update pwd failed" << std::endl;
			root["error"] = ErrorCodes::ERROR_PASSWORD;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
        }

        root["error"] = ErrorCodes::SUCCESS;
        root["email"] = email;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;

        return true;
    });

    RegisterGet("/get_contacts", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());

		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        // 从redis当中通过token获得uid
        const std::string session_token = src_root["session_token"].asString();
        const std::string device_id = src_root["device_id"].asString();
        int uid;
        if(!LogicSystem::ValidateSession(session_token, device_id, uid)) {
            root["error"] = RedisMgr::GetInstance()->IsConnected()
                                    ? ErrorCodes::ERROR_SESSION_INVALID
                                    : ErrorCodes::ERROR_REDIS;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        if (!RedisMgr::GetInstance()->IsConnected()) {
            root["error"] = ErrorCodes::ERROR_REDIS;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        std::vector<ContactInfo> contacts;
        if (!MysqlMgr::GetInstance()->GetContactListByUid(uid, contacts)) {
            root["error"] = ErrorCodes::ERROR_MYSQL;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        Json::Value contact_array(Json::arrayValue);
        for (const auto& contact : contacts) {
            const std::string presence_key = "presence:" + std::to_string(contact.uid);
            std::string status = RedisMgr::GetInstance()->HGet(presence_key, "status");
            const std::string meeting_id = RedisMgr::GetInstance()->HGet(presence_key, "meeting_id");
            if (status.empty()) {
                status = "offline";
            }
            if (!meeting_id.empty()) {
                status = "in_meeting";
            }

            Json::Value item;
            item["uid"] = contact.uid;
            item["username"] = contact.username;
            item["email"] = contact.email;
            item["display_name"] = contact.display_name;
            item["alias"] = contact.alias;
            item["remark"] = contact.remark;
            item["relation_status"] = contact.relation_status;
            // 关系和资料来自 MySQL，在线/会议状态只读 Realtime 写入的 Redis presence。
            item["status"] = status;
            contact_array.append(item);
        }

        root["error"] = ErrorCodes::SUCCESS;
        root["contacts"] = contact_array;
        root["count"] = static_cast<int>(contacts.size());
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        return true;
    });


    RegisterGet("/search_contacts", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        std::string email = src_root["keyword"].asString();
        if (email.empty()) {
            root["error"] = ErrorCodes::ERROR_JSON;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        UserInfo user;
        bool get_user = MysqlMgr::GetInstance()->GetUserInfo(email, user);
        if (!get_user) {
            root["error"] = ErrorCodes::ERROR_MYSQL;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        root["error"] = ErrorCodes::SUCCESS;
        root["uid"] = user.uid;
        root["email"] = user.email;
        root["username"] = user.username;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        return true;
    });


    RegisterPost("/add_contact", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        int contact_uid = src_root["contact_uid"].asInt();
        if(contact_uid < 0) {
            root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
        }

        // 从redis当中通过token获得uid
        const std::string session_token = src_root["session_token"].asString();
        const std::string device_id = src_root["device_id"].asString();
        int uid;
        if(!LogicSystem::ValidateSession(session_token, device_id, uid)) {
            root["error"] = RedisMgr::GetInstance()->IsConnected()
                                    ? ErrorCodes::ERROR_SESSION_INVALID
                                    : ErrorCodes::ERROR_REDIS;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        if (!RedisMgr::GetInstance()->IsConnected()) {
            root["error"] = ErrorCodes::ERROR_REDIS;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        if(!MysqlMgr::GetInstance()->AddContact(uid, contact_uid)) {
            root["error"] = ErrorCodes::ERROR_MYSQL;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        UserInfo user;
        bool get_user = MysqlMgr::GetInstance()->GetUserInfoByUid(contact_uid, user);
        if (!get_user) {
            root["error"] = ErrorCodes::ERROR_MYSQL;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        // 联系人实时状态只从 Redis presence 读取，不写入 MySQL。
        const std::string presence_key = "presence:" + std::to_string(contact_uid);
        std::string status = RedisMgr::GetInstance()->HGet(presence_key, "status");
        const std::string meeting_id = RedisMgr::GetInstance()->HGet(presence_key, "meeting_id");
        if (status.empty()) {
            status = "offline";
        }
        if (!meeting_id.empty()) {
            status = "in_meeting";
        }

        // 获得联系人的相关信息后，还要判断其的状态


        root["error"] = ErrorCodes::SUCCESS;
        root["uid"] = user.uid;
        root["email"] = user.email;
        root["username"] = user.username;
        root["status"] = status;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        return true;
    });


    RegisterPost("/delete_contact", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        int contact_uid = src_root["contact_uid"].asInt();
        if(contact_uid < 0) {
            root["error"] = ErrorCodes::ERROR_JSON;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
        }

        // 从redis当中通过token获得uid
        const std::string session_token = src_root["session_token"].asString();
        const std::string device_id = src_root["device_id"].asString();
        int uid;
        if(!LogicSystem::ValidateSession(session_token, device_id, uid)) {
            root["error"] = RedisMgr::GetInstance()->IsConnected()
                                    ? ErrorCodes::ERROR_SESSION_INVALID
                                    : ErrorCodes::ERROR_REDIS;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        if(!MysqlMgr::GetInstance()->DeleteContact(uid, contact_uid)) {
            root["error"] = ErrorCodes::ERROR_MYSQL;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

        root["error"] = ErrorCodes::SUCCESS;
        root["contact_uid"] = contact_uid;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        return true;
    });
}
