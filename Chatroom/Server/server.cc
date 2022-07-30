#include "server.hpp"
#include <hiredis/hiredis.h>
#include <string>
#include <sys/epoll.h>


void my_error(const char* errorMsg);  //错误函数
void taskfunc(void * arg);            //处理一条命令的任务函数

using namespace std;

Redis redis;

int main(){
    // 连接redis服务端
    struct timeval timeout = {1, 500000};
    redis.connect(timeout);    //超时连接
    // 将每个账号的在线状态改为-1
    int num = redis.scard("accounts");
    redisReply **allAccounts = redis.smembers("accounts");
    for(int i = 0; i < num; i++){
        redis.hsetValue(allAccounts[i]->str, "在线状态", "-1");
    }

    ThreadPool<Argc_func> pool(2,10); // 创建一个线程池类
    TcpServer sfd_class;                             // 创建服务器的socket
    map<string, int> uid_cfd;                        // 一个uid对应一个cfd的表
    int ret;                                         // 检测返回值
    ret = sfd_class.setListen(6666);           // 设置监听返回监听符.内部报错
    if(ret == -1) {exit(1);}

    // 创建epoll实例，并把listenfd加进去，监视可读事件
    int epfd = epoll_create(5);
    if(epfd == -1) { exit(1); }
    struct epoll_event temp,ep[1024];
    temp.data.fd = sfd_class.getfd();
    temp.events = EPOLLIN;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, sfd_class.getfd(), &temp);
    if(ret == -1) {
        my_error("epoll_ctl() failed.");
    }
    
    // 循环监听自己的符看是否有连接请求，监听客户端的符看是否有消息需要处理
    while(true) {
        int readyNum = epoll_wait(epfd, ep, 1024, -1);  // 有几个符就绪了
        for (int i = 0; i < readyNum; i++){  // 对于ep中每个就绪的符
             // 如果是服务器的符，说明新客户端连接，接入连接并把客户端的符扔进epoll
            if (ep[i].data.fd == sfd_class.getfd()) { 
                TcpSocket* cfd_class = sfd_class.acceptConn(NULL);
                temp.data.fd = cfd_class->getfd();
                temp.events = EPOLLIN;
                epoll_ctl(epfd,EPOLL_CTL_ADD,cfd_class->getfd(),&temp);
            }// 如果是客户端的符，就接收消息，并处理
            else {
                TcpSocket cfd_class(ep[i].data.fd);   // 用这个符创一个类来交互信息
                string command_string = cfd_class.recvMsg(); // 接收命令json字符串
                // 判断是不是通知套接字，是就加到对应的用户信息里，后边不执行
                int isRecvFd = 0;
                if(command_string.size() == 4){
                    for(auto c :command_string){
                        if(isdigit(c)){
                            isRecvFd++;
                        }
                    }
                    if(isRecvFd == 4){
                        redis.hsetValue(command_string, "通知套接字", to_string(ep[i].data.fd));
                        continue;
                    }
                }     // 如果不是通知套接字,那就是客户端，如过客户端挂了，socket类里关fd,并修改用户信息，后面不执行
                else if(command_string == "close" || command_string == "-1"){      // 如果客户端挂了，socket类里关fd,并修改用户信息
                    cout << "客户端断开连接" << endl;
                    string cuid = redis.gethash("fd-uid对应表", to_string(ep[i].data.fd));
                    redis.hsetValue(cuid, "在线状态", "-1");
                    redis.hsetValue(cuid, "通知套接字", "-1");
                    epoll_ctl(epfd,EPOLL_CTL_DEL,cfd_class.getfd(),&temp);
                    continue;
                }
                Command command;
                command.From_Json(command_string);    // 命令类将json字符串格式转为josn格式，再存到command类里
                Argc_func *argc_func = new Argc_func(cfd_class, command_string);   
                // 调用任务函数，传发过来的json字符串格式过去
                pool.addTask(Task<Argc_func>(&taskfunc,static_cast<void*>(argc_func)));
            }
        }
    }

    return 0;
}
