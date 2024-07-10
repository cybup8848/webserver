#ifndef _LOG_H_
#define _LOG_H_
#include<mutex>
#include<stdarg.h>
#include<cassert>
#include<string>
#include<thread>
#include<fstream>
#include<ctime>
#include<string.h>
#include"BlockQueue.h"

template class BlockQueue<std::string>;
//class BlockQueue;

class Log{
  public:
    static Log* getInstance();

    //日志文件、日志缓冲区大小、最大行数、最长日志条队列
    bool init(const std::string&fileName,bool closeLog,int logBufSize=8192,int maxLines=5000000,int maxQueueSize=0);

    void writeLog(int level,const char *format,...);

    void flush();

  private:
    Log();
    virtual ~Log();
    void asyncWriteLog();

  private:
    static Log *_uniqueLogInstance;//单例模式

    std::string _dirName;//路径名
    std::string _logName;//log文件名
    std::ofstream _ofstream;//打开log文件的写文件流

    int _maxLines;//日志最大行数，一个文件能够保存的最大行数
    long long _lineCount;//目前已写入日志行数

    int _logBufSize;//日志缓冲区大小
    char *_logBuf;//日志缓冲区

    BlockQueue<std::string> *_logQueue;//阻塞队列
    
    int _today;//按天记录分类，记录当前时间
    
    bool _isAsync;//是否异步标志位
    bool _closeLog;//关闭日志

    std::mutex _fileMutex;//文件互斥锁
};

//替换宏的地方必须要有closeLog这个变量
#define LOG_DEBUG(format,...) if(!_closeLog) {Log::getInstance()->writeLog(0,format,##__VA_ARGS__);Log::getInstance()->flush();}
#define LOG_INFO(format,...) if(!_closeLog) {Log::getInstance()->writeLog(1,format,##__VA_ARGS__);Log::getInstance()->flush();}
#define LOG_WARN(format,...) if(!_closeLog) {Log::getInstance()->writeLog(2,format,##__VA_ARGS__);Log::getInstance()->flush();}
#define LOG_ERROR(format,...) if(!_closeLog) {Log::getInstance()->writeLog(3,format,##__VA_ARGS__);Log::getInstance()->flush();}











#endif