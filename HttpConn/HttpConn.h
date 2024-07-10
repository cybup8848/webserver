#ifndef _HTTP_CONN_H_
#define _HTTP_CONN_H_
#include<iostream>
#include<mutex>
#include<unordered_map>
#include<string>
#include<sys/errno.h>
#include<sys/wait.h>
#include<sys/epoll.h>
#include<sys/mman.h>
#include<sys/stat.h>
#include<sys/uio.h>
#include<unistd.h>
#include<fstream>
#include<mysql/mysql.h>
#include<stdio.h>

#include"../CGImysql/SqlConnectionPool.h"
#include"../log/log.h"
#include"../timer/lst_timer.h"
class HttpConn{
  public:
    static const int FILENAME_LEN=200;
    static const int READ_BUFFER_SIZE=2048;
    static const int WRITE_BUFFER_SIZE=1024;

    enum METHOD{
      GET=0,
      POST,
      HEAD,
      PUT,
      DELETE,
      TRACE,
      OPTIONS,
      CONNECT,
      PATH
    };

    enum CHECK_STATE{
      CHECK_STATE_REQUESTLINE = 0,//解析请求行
      CHECK_STATE_REQUESTHEADER,//解析请求头
      CHECK_STATE_REQUESTBODY//解析消息体，仅用于解析POST请求
    };

    //表示HTTP请求的处理结果，在头文件中初始化了八种情形，在报文解析时只涉及到四种。
    enum HTTP_CODE{
      NO_REQUEST,//请求不完整，需要继续读取请求报文数据
      GET_REQUEST,//获得了完整的HTTP请求
      BAD_REQUEST,//HTTP请求报文有语法错误
      NO_RESOURCE,//
      FORBIDDEN_REQUEST,
      FILE_REQUEST,
      INTERNAL_ERROR,//服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
      CLOSED_CONNECTION
    };

    enum LINE_STATUS{
      LINE_OK = 0,//完整读取一行
      LINE_BAD,//报文语法有无
      LINE_OPEN//读取的行不完整
    };

    public:
      HttpConn(){}
      ~HttpConn(){}

    public:
      void init(int sockFd,const struct sockaddr_in &addr,char *,int,bool,std::string user,std::string password,std::string dbName);

      void closeConn(bool realClose=true);

      void process();

      bool readOnce();

      bool write();

      struct sockaddr_in *getAddress();

      void initUserPasswordMap(ConnectionPool *connPool);

      int _timerFlag;

      int _improve;

    private:
      void init();

      HTTP_CODE processRead();//处理读

      bool processWrite(HTTP_CODE ret);//处理写

      //http请求报文由三部分组成：请求行、请求头、请求体
      HTTP_CODE parseRequestLine(char *text);
      HTTP_CODE parseRequestHeader(char *text);
      HTTP_CODE parseRequestBody(char *text);
      HTTP_CODE doRequest();//做出请求
      
      char *getLine();
      LINE_STATUS parseLine();

      void unmap();

      //http响应报文也由三部分组成：响应行、响应头、响应体
      bool addResponse(const char *format,...);
      bool addResponseLine(int status,const char *title);
      bool addResponseHeader(int bodyLength);
      bool addResponseBody(const char *body);
      
      bool addResponseBodyType();
      bool addResponseBodyLength(int bodyLength);
      bool addLinger();
      bool addBlankLine();

    public:
      static int _epollFd;
      static int _userCount;
      MYSQL *_mysql;
      int _state;//0读，1写

    private:
      int _sockFd;
      struct sockaddr_in _address;

      char _readBuf[READ_BUFFER_SIZE];//存储读取的请求报文数据
      long int _readIndex;//缓冲区中_readBuf数据的最后一个字节的下一个位置
      long int _checkIndex;//_readBuf读取的位置_checkIndex
      int _startLine;//_readBuf中已经解析的字符个数

      char _writeBuf[WRITE_BUFFER_SIZE];//存储发出的响应报文数据
      long int _writeIndex;//指示_writeBuf中的长度

      CHECK_STATE _checkState;//主状态机的状态
      METHOD _method;//请求方法

      //以下为解析请求报文中对应的6个变量
      char *_url;
      char *_version;
      char *_host;
      long int _bodyLength;
      bool _linger;//是否保持长连接，连接是否复用
      //这里具体的含义是有关http 请求的是否保持长连接，即链接是否复用，每次请求是复用已建立好的请求，还是重新建立一个新的请求。
      
      char _realFile[FILENAME_LEN];//存储读取文件的名称
      char *_fileAddress;//读取文件经过内存映射后的地址
      struct stat _fileStat;//读取文件的信息
      struct iovec _iv[2];//io向量机制iovec
      int _ivCount;

      bool _cgi;//是否启用POST
      char *_requestBody;//存储请求体数据
      
      int _bytes_to_send;//剩余发送字节数
      int _bytes_have_send;//已发送字节数

      char *_docRoot;//网站根目录，文件夹内存放请求的资源和跳转的html文件
      
      int _trigMode;//触发模式
      bool _closeLog;

      std::string _sqlUser;
      std::string _sqlPassword;
      std::string _dbName;
};

#endif