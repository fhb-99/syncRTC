#pragma once

#include "common/global.h"
#include "service/LogicSystem.h"

#include <iostream>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

class LogicSystem;

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
    friend class LogicSystem;
public:
    HttpConnection(boost::asio::io_context& ioc);
    void start();
    boost::asio::ip::tcp::socket& GetSocket();
private:
    boost::asio::ip::tcp::socket _socket;
    //检查连接是否超时
    void CheckDeadline();
    void WriteResponse();
    void HandleRequest();

    // 解析请求所带的参数
	void PreParseGetParam();
     // The buffer for performing reads.
    beast::flat_buffer  _buffer{ 8192 };

    // The request message.
    http::request<http::dynamic_body> _request;

    // The response message.
    http::response<http::dynamic_body> _response;

    // The timer for putting a deadline on connection processing.
    net::steady_timer deadline_{
        _socket.get_executor(), std::chrono::seconds(60) };

    std::string m_get_url;
	std::unordered_map<std::string, std::string> m_get_params;
};