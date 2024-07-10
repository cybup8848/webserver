#ifndef _WEB_SERVER_H_
#define _WEB_SERVER_H_
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/epoll.h>

#include"../threadpool/threadpool.h"
#include"../HttpConn/HttpConn.h"

const int MAX_FD=65536;//最大文件描述符
const int MAX_EVENT_NUMBER=10000;//最大事件数
const int TIMESLOT=5;//最小超时单位

class WebServer{
  public:
    WebServer();
    ~WebServer();

    void init(int port,std::string user,std::string password,std::string dbName,bool logWrite,bool optLinger,int trigMode,
      int sqlNum,int threadNum,bool closeLog,int actorModel);

    void threadPool();
    void sqlPool();
    void logWrite();
    void trigMode();
    void eventListen();
    void eventLoop();
    void timer(int connFd,struct sockaddr_in clientAddress);
    void adjustTimer(UtilTimer *timer);
    void dealTimer(UtilTimer *timer,int sockFd);
    bool dealClientData();
    bool dealWithSignal(bool &timeOut,bool &stopServer);
    void dealWithRead(int sockFd);
    void dealWithWrite(int sockFd);

  private:
    //基础
    int _port;
    char *_root;
    bool _asyncLog;
    bool _closeLog;
    int _actorModel;

    int _pipeFd[2];
    int _epollFd;
    HttpConn *_httpConns;

    //数据库相关
    ConnectionPool *_connPool;//数据库连接池
    std::string _user;//用户名
    std::string _password;//密码
    std::string _dbName;//数据库名
    int _sqlNum;//

    //线程池相关
    ThreadPool<HttpConn> *_threadPool;
    int _threadNum;

    //epoll_event相关
    epoll_event _events[MAX_EVENT_NUMBER];

    int _listenFd;//服务器端的套接字
    bool _optLinger;//是否优雅退出
    int _trigMode;//ET：1     LT：0
    int _listenTrigMode;
    int _connTrigMode;

    //定时器相关
    ClientData *_userTimer;//每个连接都有一个定时器
    Utils _utils;
};
#endif

//Log类、ConnectionPool类 都使用了单例模式