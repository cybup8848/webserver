#ifndef _LST_TIMER_H_
#define _LST_TIMER_H_

#include<sys/socket.h>
#include<netinet/in.h>

#include<sys/epoll.h>
#include<sys/errno.h>

#include<sys/fcntl.h>
#include<sys/eventfd.h>

#include<signal.h>
#include<assert.h>
#include<string.h>

#include"../HttpConn/HttpConn.h"
#include"../log/log.h"

class UtilTimer;
struct ClientData{
  struct sockaddr_in _address;
  int _sockFd;
  UtilTimer *_timer;
};

class UtilTimer{
  public:
    UtilTimer(){
      _prev=nullptr;
      _next=nullptr;
    }

  public:
    time_t _expire;
    void (*_callbackFun)(ClientData *);
    ClientData *_userData;
    UtilTimer *_prev;
    UtilTimer *_next;
};

class SortTimerList{
  public:
    SortTimerList();
    ~SortTimerList();

    void addTimer(UtilTimer *timer);
    void adjustTimer(UtilTimer *timer);
    void delTimer(UtilTimer *timer);
    void tick();//滴答，时间到，开始处理不活动的任务

  private:
    UtilTimer *_head;
    UtilTimer *_tail;
};

class Utils{
  public:
    Utils(){}
    ~Utils(){}

    void init(int timeSlot);

    int setNonBlocking(int fd);//对文件描述符设置非阻塞

    void addFd(int epollFd,int fd,bool oneShot,int trigMode);////将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT

    static void sigHandler(int sig);//信号处理函数

    void addSig(int sig,void (*handler)(int),bool restart=true);//设置信号函数

    void timerHandler();//定时处理任务，重新定时以不断触发SIGALARM信号

    void showError(int connFd,const char *info);

  public:
    static int *_pipeFd;//通过管道进行通信
    SortTimerList _timerList;
    static int _epollFd;
    int _timeSlot;//定时时间
};

void callbackFunc(ClientData *userData);

#endif


























