#include"SqlConnectionPool.h"

//初始化单例模式
ConnectionPool * ConnectionPool::_uniqueInstance=new ConnectionPool();

MYSQL* ConnectionPool::getConnection(){//获取数据库连接
  MYSQL *res=nullptr;
  {
    std::unique_lock<std::mutex> lock(_connMutex);
    _condition.wait(lock,[this]{return !this->_connectionPool.empty();});
    res=_connectionPool.front();
    _connectionPool.pop_front();

    _freeConn-=1;
    _curConn+=1;
  }
  return res;
}

bool ConnectionPool::ReleaseConnection(MYSQL *connection){//释放连接
  if(!connection){
    return true;
  }

  {
    std::unique_lock<std::mutex> lock(_connMutex);
    _connectionPool.emplace_back(connection);
    _freeConn+=1;
    _curConn-=1;
  }
  _condition.notify_one();
  return true;
}

int ConnectionPool::getFreeConn(){//获取连接
  return _freeConn;
}

void ConnectionPool::destroyPool(){//销毁所有连接
  std::unique_lock<std::mutex> lock(_connMutex);
  for(MYSQL* &mysql:_connectionPool){
    mysql_close(mysql);
  }
  _connectionPool.clear();
  _maxConn=_curConn=_freeConn=0;
  _closeLog=false;
}

void ConnectionPool::init(const std::string &url,int port,
      const std::string &user,const std::string &password,
      const std::string &dbName,bool closeLog,int maxConn){
  _url=url;//主机ip地址
  _port=port;//主机连接端口号
  _user=user;//登录用户名
  _password=password;//登录密码
  _dbName=dbName;//数据库名
  _closeLog=closeLog;//日志开关

  for(int i=0;i<maxConn;i++){
    MYSQL *mysql=mysql_init(nullptr);
    if(!mysql){
      LOG_ERROR("MySQL Error");
      exit(1);
    }

    mysql=mysql_real_connect(mysql,_url.c_str(),_user.c_str(),_password.c_str(),_dbName.c_str(),_port,nullptr,0);
    if(!mysql){
      LOG_ERROR("MySQL Error");
      exit(1);
    }
    _connectionPool.emplace_back(mysql);
    ++_freeConn;
  }
  
  _maxConn=_freeConn;
}

ConnectionPool::ConnectionPool(){
  _freeConn=0;
  _curConn=0;
}

ConnectionPool::~ConnectionPool(){
  destroyPool();
}

ConnectionPool* ConnectionPool::GetInstance(){
  return _uniqueInstance;
}


ConnectionRAII::ConnectionRAII(MYSQL* &con,ConnectionPool *&connPool){
  _myConn=connPool->getConnection();
  con=_myConn;
  _connPool=connPool;
}

ConnectionRAII::~ConnectionRAII(){
  _connPool->ReleaseConnection(_myConn);
}
