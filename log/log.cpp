#include"log.h"

Log* Log::_uniqueLogInstance=new Log();

Log* Log::getInstance(){
  return _uniqueLogInstance;
}

//日志文件、日志缓冲区大小、最大行数、最长日志条队列
//若设置阻塞队列大小大于0则选择异步日志，等于0则选择同步日志，更新isAysnc变量。
bool Log::init(const std::string&fileName,bool closeLog,int logBufSize,int maxLines,int maxQueueSize){
  if(maxQueueSize>0){
    _isAsync=true;
    _logQueue=new BlockQueue<std::string>(maxQueueSize);
    std::thread th(&Log::asyncWriteLog,this);
  }

  _logBufSize=logBufSize;
  _logBuf=new char[logBufSize]{0};

  _closeLog=closeLog;
  _maxLines=maxLines;

  size_t pos=fileName.find_last_of("/");
  time_t now=time(nullptr);
  struct tm *date=localtime(&now);
  _today=date->tm_mday;

  if(pos==std::string::npos){//表示没有找到
    _logName=fileName;
  } else {
    _dirName=fileName.substr(0,pos+1);
    _logName=fileName.substr(pos+1);
  }
  //拼接成日志文件名
  std::string logFullName=_dirName+std::to_string(date->tm_year+1900)+"_"+std::to_string(date->tm_mon+1)+"_"+std::to_string(date->tm_mday)+"_"+_logName;
  
  //打开文件
  _ofstream.open(logFullName,std::ios::app);
  return _ofstream.is_open();
}

void Log::writeLog(int level,const char *format,...){
  std::string infoType("");
  switch(level){
    case 0:
      infoType+="[debug]:";
      break;
    case 1:
      infoType+="[info]:";
      break;
    case 2:
      infoType+="[warn]:";
      break;
    case 3:
      infoType+="[error]:";
      break;
    default:
      infoType+="[info]:";
      break;
  }

  time_t now=time(nullptr);
  struct tm *date=localtime(&now);
  {
    std::unique_lock<std::mutex> lock(_fileMutex);
    if((_today!=date->tm_mday)||(_lineCount!=0&&_lineCount%_maxLines==0)){
      _ofstream.flush();
      _ofstream.close();
    
      std::string logFullName("");
      std::string dateStr=std::to_string(date->tm_year+1900)+"_"+std::to_string(date->tm_mon+1)+"_"+std::to_string(date->tm_mday)+"_";
    
      if(_today!=date->tm_mday){
        logFullName=logFullName+_dirName+dateStr+_logName;
        _today=date->tm_mday;
        _lineCount=0;
      } else {
        logFullName=logFullName+_dirName+dateStr+_logName+"."+std::to_string(_lineCount/_maxLines);
      }

      _ofstream.open(logFullName,std::ios::app);
    }
  }

  std::string logInfo=std::to_string(date->tm_year+1900)+"-"+std::to_string(date->tm_mon+1)+"-"+std::to_string(date->tm_mday)+" "+
      std::to_string(date->tm_hour)+":"+std::to_string(date->tm_min)+":"+std::to_string(date->tm_sec)+" "+infoType;
  
  {
    std::unique_lock<std::mutex> lock(_fileMutex);
    memset(_logBuf,'\0',_logBufSize);
    va_list args;
    va_start(args,format);
    snprintf(_logBuf,_logBufSize-1,format,args);
    va_end(args);
    logInfo+=std::string(_logBuf);
  }
  
    
  if(_isAsync&&!_logQueue->full()){
    _logQueue->push(logInfo);
  } else {
    std::unique_lock lock(_fileMutex);
     _ofstream<<logInfo<<std::endl;
  }
}

void Log::flush(){ 
  std::unique_lock<std::mutex> lock(_fileMutex);
  _ofstream.flush();
}

Log::Log(){
  _lineCount=0;
  _isAsync=false;
  _logBuf=nullptr;
  _logBufSize=0;
}

Log::~Log(){
  if(_logQueue){
    delete _logQueue;
    _logQueue=nullptr;
  }

  if(_logBuf){
    delete[] _logBuf;
    _logBuf=nullptr;
    _logBufSize=0;
  }

  if(_ofstream.is_open()){
    _ofstream.close();
  }
}
    
void Log::asyncWriteLog(){
  std::string str("");
  for(;;){
    _logQueue->pop(str);
    {
      std::unique_lock<std::mutex> lock(_fileMutex);
      _ofstream<<str;
    }
  }
}





