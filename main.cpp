#include"./config/config.h"
int main(int argc,char *argv[]){
  //需要修改数据库信息，登录名，密码，数据库名
  std::string user("cyb");
  std::string password("123");
  std::string dbName("users");

  //命令行解析
  Config config;
  config.parseArg(argc,argv);

  //初始化
  WebServer webServer;
  webServer.init(config._port,user,password,dbName,config._closeLog,config._optLinger,config._trigMode,
    config._sqlNum,config._threadNum,config._closeLog,config._actorModel);

  webServer.logWrite();//日志

  webServer.sqlPool();//数据库池

  webServer.threadPool();//线程池

  webServer.trigMode();//触发模式

  webServer.eventListen();//监听

  webServer.eventLoop();//运行

  return 0;
}



















