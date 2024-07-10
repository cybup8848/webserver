#include"config.h"
Config::~Config(){}

Config::Config(){
  //端口号,默认9006
    _port = 9006;

    //日志写入方式，默认同步
    _logWrite = 0;

    //触发组合模式,默认listenfd LT + connfd LT
    _trigMode = 0;

    //listenfd触发模式，默认LT
    _listenTrigMode = 0;    //没有通过命令行参数给定

    //connfd触发模式，默认LT
    _connTrigMode = 0;      //没有通过命令行参数给定

    //优雅关闭链接，默认不使用
    _optLinger = 0;

    //数据库连接池数量,默认8
    _sqlNum = 8;

    //线程池内的线程数量,默认8
    _threadNum = 8;

    //关闭日志,默认不关闭
    _closeLog = 0;

    //并发模型,默认是proactor
    _actorModel = 0;
}

void Config::parseArg(int argc,char *argv[]){
  int opt=0;
  const char *str="p:l:m:o:s:t:c:a:";
  while((opt=getopt(argc,argv,str))!=-1){
    switch(opt){
      case 'p':
      {
        _port=std::atoi(optarg);
        break;
      }
      case 'l':
      {
        _logWrite=atoi(optarg);
        break;
      }
      case 'm':
      {
        _trigMode=atoi(optarg);
        break;
      }
      case 'o':
      {
        _optLinger=atoi(optarg);
        break;
      }
      case 's':
      {
        _sqlNum=atoi(optarg);
        break;
      }
      case 't':
      {
        _threadNum=atoi(optarg);
        break;
      }
      case 'c':
      {
        _closeLog=atoi(optarg);
        break;
      }
      case 'a':
      {
        _actorModel=atoi(optarg);
        break;
      }
      default:
        break;
    }
  }
}

















