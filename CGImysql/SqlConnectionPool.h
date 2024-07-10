#ifndef _SQL_CONNECTION_POOL_H_
#define _SQL_CONNECTION_POOL_H_
#include<mutex>
#include<list>
#include<condition_variable>
#include<mysql/mysql.h>
#include"../log/log.h"

class ConnectionPool{
  public:
    MYSQL *getConnection();//获取数据库连接
    bool ReleaseConnection(MYSQL *connection);//释放连接
    int getFreeConn();//获取连接
    void destroyPool();//销毁所有连接

    void init(const std::string &url,int port,
      const std::string &user,const std::string &password,
      const std::string &dbName,bool closeLog,int maxConn);

    static ConnectionPool *GetInstance();
  private:
    ConnectionPool();
    ~ConnectionPool();

  private:
    int _freeConn;//空闲连接
    int _maxConn;//最大连接
    int _curConn;//目前的连接数

    std::list<MYSQL *> _connectionPool;//连接池

    std::mutex _connMutex;
    std::condition_variable _condition;

  private:
    static ConnectionPool *_uniqueInstance;//单例模式

  public:
    std::string _url;//主机ip地址
    int _port;//主机连接端口号
    std::string _user;//登录用户名
    std::string _password;//登录密码
    std::string _dbName;//数据库名
    bool _closeLog;//日志开关
};

class ConnectionRAII{
  public:
    ConnectionRAII(MYSQL* &con,ConnectionPool *&connPool);
    ~ConnectionRAII();
  private:
    MYSQL *_myConn;
    ConnectionPool *_connPool;
};

















#endif