// 这个代码主要是做业务的，为了将网络模块和业务模块解耦
#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <functional>
#include <mutex>
using namespace std;
using namespace muduo;
using namespace muduo::net;

#include "redis.hpp"
#include "usermodel.hpp"
#include "json.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
using json = nlohmann::json;

// 处理消息的事件回调方法类型
using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;

// 聊天服务器业务类
// 单例模式
class Chatservice
{
    public:
        // 获取单例对象的接口函数
        static Chatservice* instance();
        // 登录
        void login(const TcpConnectionPtr &conn, json &js, Timestamp time);
        // 注册
        void reg(const TcpConnectionPtr &conn, json &js, Timestamp time);
        // 
        void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time);
        // 一对一聊天业务
        void oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
        // 获取消息对应的处理器
        void addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time);
        // 创建群组业务
        void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
        // 加入群组业务
        void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
        // 群组聊天业务
        void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);

        

        // 获取消息对应的处理器
        MsgHandler getHandler(int msgid);
        // 处理客户端异常退出
        void clientCloseException(const TcpConnectionPtr &conn);
        // 服务器异常，业务重置方法
        void reset();

        void handleRedisSubscribeMessage(int userid, string msg);
    private:
        Chatservice();

        // 存储消息id和其对应的业务处理方法
        unordered_map<int, MsgHandler> _msgHandlerMap;

        // 存储在线用户的通信连接
        unordered_map<int, TcpConnectionPtr> _userConnMap;

        // 定义互斥锁，保证_userConnMap的线程安全
        mutex _connMutex;

        // 数据操作类对象
        // 用户
        UserModel _userModel;
        // 离线消息
        OfflineMsgModel _offlineMsgModel;
        // 好友业务
        FriendModel _friendModel;
        // 群组业务
        GroupModel _groupModel;

        // redis操作对象
        Redis _redis;
};


#endif