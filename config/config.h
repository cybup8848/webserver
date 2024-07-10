#ifndef _CONFIG_H_
#define _CONFIG_H_
#include"../WebServer/WebServer.h"
class Config{
  public:
    Config();
    ~Config();

    void parseArg(int argc,char *argv[]);

    int _port;//端口号

    int _logWrite;//日志写入方式

    int _trigMode;//触发组合模式，listenTrigMode、connTrigMode

    int _listenTrigMode;//listenFd触发模式

    int _connTrigMode;//connFd触发模式

    int _optLinger;//是否优雅关闭

    int _sqlNum;//数据库连接池数量

    int _threadNum;//线程池内的线程数量

    int _closeLog;//是否关闭日志

    int _actorModel;//并发模型选择
};
#endif