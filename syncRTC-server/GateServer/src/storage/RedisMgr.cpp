#include "storage/RedisMgr.h"
#include "config/ConfigMgr.h"

namespace {
// RAII：确保连接一定归还连接池
class RedisConnGuard {
public:
    RedisConnGuard(RedisConPool* pool, std::shared_ptr<sw::redis::Redis> conn)
        : pool_(pool), conn_(std::move(conn)) {}
    RedisConnGuard(const RedisConnGuard&) = delete;
    RedisConnGuard& operator=(const RedisConnGuard&) = delete;
    ~RedisConnGuard() {
        if (pool_ && conn_) pool_->returnConnection(std::move(conn_));
    }
    sw::redis::Redis* operator->() const { return conn_.get(); }
    explicit operator bool() const { return static_cast<bool>(conn_); }
private:
    RedisConPool* pool_;
    std::shared_ptr<sw::redis::Redis> conn_;
};
} // namespace

RedisMgr::~RedisMgr()
{
    Close();
}

bool RedisMgr::Connect(const std::string &host, int port, const std::string &password)
{
    try
    {
        // 重建连接池（每个连接在构造时完成 auth + ping）
        // pool size 暂按默认 5（如需可改为配置项）
        _con_pool.reset(new RedisConPool(5, host.c_str(), port, password.c_str()));
        _is_connected = (_con_pool && _con_pool->Healthy());
        if (_is_connected) {
            std::cout << "Redis pool connect success: " << host << ":" << port << std::endl;
        }
        return _is_connected;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        _is_connected = false;
        return false;
    }
    
}

bool RedisMgr::Get(const std::string &key, std::string &value)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        auto opt_val = redis->get(key);
        if (opt_val) 
        {
            value = *opt_val;
            return true;
        }
        // key 不存在不算异常，返回 false
        return false;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis Get error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::Set(const std::string &key, const std::string &value, int expire_seconds)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        if (expire_seconds > 0) 
        {
            redis->set(key, value, std::chrono::seconds(expire_seconds));
        } 
        else 
        {
            redis->set(key, value);
        }
        return true;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis Set error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::Auth(const std::string &password)
{
    // 连接池模式下，每个连接在创建时已完成认证；
    // 这里保留接口兼容：仅用于手动重建连接池时传入密码。
    if (!_con_pool) return false;
    try 
    {
        // 不对已创建连接逐个 auth（redis-plus-plus 连接是独立的）。
        // 需要变更密码时请调用 Connect(host, port, password) 重建连接池。
        (void)password;
        return true;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis auth failed: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::LPush(const std::string &key, const std::string &value)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        redis->lpush(key, value);
        return true;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis LPush error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::LPop(const std::string &key, std::string &value)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        auto opt_val = redis->lpop(key);
        if (opt_val) 
        {
            value = *opt_val;
            return true;
        }
        return false;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis LPop error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::RPush(const std::string &key, const std::string &value)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        redis->rpush(key, value);
        return true;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis RPush error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::RPop(const std::string &key, std::string &value)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        auto opt_val = redis->rpop(key);
        if (opt_val) 
        {
            value = *opt_val;
            return true;
        }
        return false;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis RPop error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::HSet(const std::string &key, const std::string &hkey, const std::string &value)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        redis->hset(key, hkey, value);
        return true;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis HSet error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::HSet(const char *key, const char *hkey, const char *hvalue, size_t hvaluelen)
{
    if (!_con_pool || !key || !hkey || !hvalue) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        // 转换为 string（避免内存越界）
        std::string s_key(key);
        std::string s_hkey(hkey);
        std::string s_hvalue(hvalue, hvaluelen);
        redis->hset(s_key, s_hkey, s_hvalue);
        return true;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis HSet (char*) error: " << e.what() << std::endl;
        return false;
    }
}

std::string RedisMgr::HGet(const std::string &key, const std::string &hkey)
{
    if (!_con_pool) return "";
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return "";
        auto opt_val = redis->hget(key, hkey);
        return opt_val ? *opt_val : "";
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis HGet error: " << e.what() << std::endl;
        return "";
    }
}

bool RedisMgr::Del(const std::string &key)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        // del 返回删除数量，但对上层通常不区分“key 不存在”
        redis->del(key);
        return true;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis Del error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::ExistsKey(const std::string &key)
{
    if (!_con_pool) return false;
    try 
    {
        RedisConnGuard redis(_con_pool.get(), _con_pool->getConnection());
        if (!redis) return false;
        // exists 返回数量（0/1）
        return redis->exists(key) > 0;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Redis ExistsKey error: " << e.what() << std::endl;
        return false;
    }
}

void RedisMgr::Close()
{
    if (_con_pool) _con_pool->Close();
    _is_connected = false;
}

RedisMgr::RedisMgr()
{
    auto& gCfgMgr = ConfigMgr::Init();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Password"];
    _con_pool.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
    _is_connected = (_con_pool && _con_pool->Healthy());
}