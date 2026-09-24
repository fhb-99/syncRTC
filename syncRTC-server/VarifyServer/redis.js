const config_module = require('./config')
const Redis = require('redis');

// 创建 Redis 客户端
const RedisCli = Redis.createClient({
    socket: {
        host: config_module.redis_host,     // Redis 服务器主机名
        port: config_module.redis_port,     // Redis 服务器端口号
    },
    password: config_module.redis_passwd,     // Redis 服务器密码
});

// 连接状态标志
let isConnected = false;

// 监听连接成功事件
RedisCli.on('connect', function() {
    console.log('Redis client connected');
    isConnected = true;
});

// 监听错误事件
RedisCli.on('error', function(err) {
    console.log("RedisCli connect error:", err);
    isConnected = false;
});

// 监听连接关闭事件
RedisCli.on('end', function() {
    console.log('Redis client connection ended');
    isConnected = false;
});

// 初始化连接
async function initRedisConnection() {
    try {
        await RedisCli.connect();
        console.log('Redis connection initialized successfully');
    } catch (error) {
        console.log('Failed to initialize Redis connection:', error);
    }
}

// 启动时初始化连接
initRedisConnection();


// 检查并确保Redis连接
async function ensureConnection() {
    if (!isConnected) {
        try {
            await RedisCli.connect();
            console.log('Redis reconnected');
        } catch (error) {
            console.log('Failed to reconnect Redis:', error);
            throw error;
        }
    }
}

// 根据key获取value
async function GetRedis(key){
    try{
        await ensureConnection();
        const result = await RedisCli.get(key)
        if(result === null){
          console.log('result:','<'+result+'>', 'This key cannot be find...')
          return null
        }
        console.log('Result:','<'+result+'>','Get key success!...');
        return result
    }catch(error){
        console.log('GetRedis error is', error);
        return null
    }
}

/**
 * 根据key查询redis中是否存在key
 * @param {*} key
 * @returns
 */
async function QueryRedis(key) {
    try{
        await ensureConnection();
        const result = await RedisCli.exists(key)
        //  判断该值是否为空 如果为空返回null
        if (result === 0) {
          console.log('result:<','<'+result+'>','This key is null...');
          return null
        }
        console.log('Result:','<'+result+'>','With this value!...');
        return result
    }catch(error){
        console.log('QueryRedis error is', error);
        return null
    }

  }

/**
 * 设置key和value，并过期时间
 * @param {*} key
 * @param {*} value
 * @param {*} exptime
 * @returns
 */
async function SetRedisExpire(key,value, exptime){
    try{
        await ensureConnection();
        // 设置键和值
        await RedisCli.set(key,value)
        // 设置过期时间（以秒为单位）
        await RedisCli.expire(key, exptime);
        return true;
    }catch(error){
        console.log('SetRedisExpire error is', error);
        return false;
    }
}

/**
 * 退出函数
 */
// function Quit(){
//     RedisCli.quit();
// }

module.exports = {GetRedis, QueryRedis, SetRedisExpire,}
