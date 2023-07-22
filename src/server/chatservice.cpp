#include "chatservice.hpp"
#include "public.hpp"
#include "usermodel.hpp"
// #include "json.hpp"

#include <muduo/base/Logging.h>
using namespace muduo;
// using json nlohmann::json;

// 获取单例对象的接口函数
Chatservice* Chatservice::instance(){
    static Chatservice service;
    return &service;

}
// 注册消息以及对应的Handler回调操作
Chatservice::Chatservice(){
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&Chatservice::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&Chatservice::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&Chatservice::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&Chatservice::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&Chatservice::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&Chatservice::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&Chatservice::groupChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&Chatservice::loginout, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&Chatservice::handleRedisSubscribeMessage, this, _1, _2));
    }
}

MsgHandler Chatservice::getHandler(int msgid){
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if(it == _msgHandlerMap.end()){
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp time) {
            LOG_ERROR << "msgid:" << msgid << "can not find handler!";
        };        
    }
    else{
        return _msgHandlerMap[msgid];
    }
    
}

// 处理登录业务
// 
void Chatservice::login(const TcpConnectionPtr &conn, json &js, Timestamp){
    // LOG_INFO << "do login service!!!";
    int id = js["id"];
    string pwd = js["password"];

    User user = _userModel.query(id);
    if(user.getId()==id && user.getPwd()==pwd){
        if(user.getState() == "online"){
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该账户已经登录！";
            conn->send(response.dump()); 
        }else{
            // 登录成功
            {
                // 加锁以保证线程安全
                lock_guard<mutex> lock(_connMutex);
                // 记录用户的连接信息
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id); 
            
            // 登录成功
            // 更新用户状态信息
            user.setState("online");
            _userModel.updateState(user);
            
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if(!vec.empty()){
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后，把该用户的所有离线消息删除
                _offlineMsgModel.remove(id);
            }
            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if(!userVec.empty()){
                vector<string> vec2;
                for(User &user:userVec){
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;

            }

            // 查询用户的群组信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group:[{groupid:[xxx, xxx, xxx, xxx]}]
                vector<string> groupV;
                for (Group &group : groupuserVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();
                    vector<string> userV;
                    for (GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }

                response["groups"] = groupV;
            }
            conn->send(response.dump());  
        }
        
             

    }else{
        // 该用户不存在，或者用户存在密码错误
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或者密码错误";
        conn->send(response.dump()); 
    }

}
// 处理注册业务
void Chatservice::reg(const TcpConnectionPtr &conn, json &js, Timestamp time){
    // LOG_INFO << "do reg service!!!";
    string name = js["name"];
    string pwd = js["password"];
    LOG_INFO << "name: " << name;
    LOG_INFO << "pwd: " << pwd;

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if(state){
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
        
    }else{
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }

}

// 处理注销业务
void Chatservice::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid); 

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

// 处理客户端异常退出
void Chatservice::clientCloseException(const TcpConnectionPtr &conn){
    {
        User user;
        {
            // 加锁以保证线程安全
            lock_guard<mutex> lock(_connMutex);
            
            for(auto it = _userConnMap.begin(); it!=_userConnMap.end(); ++it){
                if(it->second == conn){
                    // 从map表删除用户的连接信息
                    user.setId(it->first);
                    _userConnMap.erase(it);
                    break;
                }
            }
        }

        // 用户注销，相当于就是下线，在redis中取消订阅通道
        _redis.unsubscribe(user.getId()); 

        // 更新用户的状态信息
        if(user.getId() != -1){
            user.setState("offline");
            _userModel.updateState(user);
        }

    }

}

// 服务器异常，业务重置方法
void Chatservice::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

// 一对一聊天业务
void Chatservice::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time){
    // LOG_INFO << "------------oneChat: -----------------";
    int toid = js["to"].get<int>();
    // LOG_INFO << "------------oneChat: -----------------";
    {
        lock_guard<mutex> lock(_connMutex);

        // 查询聊天对象是否在该服务器上在线
        auto it = _userConnMap.find(toid);
        if(it != _userConnMap.end()){           
            // toid在线，转发消息 服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;

        }
    }
    // 如果该聊天对象不在该服务器上在线，则可能在其他服务器上在线
    // 查询toid是否在线
    User user = _userModel.query(toid);
    if(user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
    }

    // toid不在线， 存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
    
}
// 添加好友业务
void Chatservice::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time){
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();
    // LOG_INFO << "name: " << userid;
    // LOG_INFO << "pwd: " << friendid;

    // 存储好友信息
    _friendModel.insert(userid, friendid);

}

// 创建群组业务
void Chatservice::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{    
    LOG_INFO << "------------------createGroup-----------";
    int userid = js["id"].get<int>();
    LOG_INFO << "name: " << userid;
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.CreateGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务
void Chatservice::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}


// 群组聊天业务
void Chatservice::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else
        {
            // 查询toid是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 若不在线，存储离线群消息
                _offlineMsgModel.insert(id, js.dump());

            }            
        }
    }
}

// 从redis消息队列中获取订阅的消息
void Chatservice::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}