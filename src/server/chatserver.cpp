#include "chatserver.hpp"
#include "json.hpp"
#include <muduo/base/Logging.h>
#include "chatservice.hpp"

#include <functional>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg) : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册连接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    //
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    _server.setThreadNum(4);
}

void ChatServer::start()
{
    _server.start();
}

void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端端口连接
    if(!conn->connected()){
        // 客户端异常关闭
        LOG_INFO << "disconnected: ";
        Chatservice::instance()->clientCloseException(conn);

        conn->shutdown();
    }

}

void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    string buf = buffer->retrieveAllAsString();
    // 数据反序列话
    json js = json::parse(buf);

    
    // 我们需要完全解耦网络模块的代码和业务模块的代码
    // 通过js["msgid"]获取业务handler
    auto msgHandler = Chatservice::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定号的事件处理器，来执行相应的业务处理。
    // gy:这里不是很理解，为什么业务层还要使用回调函数，onMessage已经是在回调了，即消息发生后执行了，
    // gy：这时候应该不存在所谓的“我不知道登录或注册消息什么时候来这种情况了”
    // gy：也就是说这里完全可以用if-else判断msgid来决定处理登录还是注册。
    msgHandler(conn, js, time);
    
}