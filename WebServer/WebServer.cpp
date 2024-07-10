#include "WebServer.h"
WebServer::WebServer() {
  _httpConns = new HttpConn[MAX_FD];    // http_conn类对象
  _userTimer = new ClientData[MAX_FD];  // 每一个连接都有一个定时器；

  // root文件夹路径
  char serverPath[200] = {0};
  getcwd(serverPath, 200);  // get current work directory
  const char *root = "/root";
  _root = new char[strlen(serverPath) + strlen(root) + 1];
  strcpy(_root, serverPath);
  strcat(_root, root);
}

WebServer::~WebServer() {
  close(_pipeFd[0]);
  close(_pipeFd[1]);
  close(_epollFd);
  close(_listenFd);

  if (_root) {
    delete[] _root;
    _root = nullptr;
  }

  if (_httpConns) {
    delete[] _httpConns;
    _httpConns = nullptr;
  }

  if (_userTimer) {
    delete[] _userTimer;
    _userTimer = nullptr;
  }

  if (_threadPool) {
    delete _threadPool;
    _threadPool = nullptr;
  }
}

void WebServer::init(int port, std::string user, std::string password,
                     std::string dbName, bool logWrite, bool optLinger,
                     int trigMode, int sqlNum, int threadNum, bool closeLog,
                     int actorModel) {
  _port = port;
  _user = user;
  _password = password;
  _dbName = dbName;
  _asyncLog = logWrite;  // 异步写入日志，还是同步写入日志
  _optLinger = optLinger;
  _trigMode = trigMode;

  _sqlNum = sqlNum;  // 数据库连接池中，最大连接数
  _threadNum = threadNum;

  _closeLog = closeLog;
  _actorModel = actorModel;
}

void WebServer::threadPool() {
  _threadPool = new ThreadPool<HttpConn>(_actorModel, _connPool, _threadNum);
}

void WebServer::sqlPool() {
  _connPool = ConnectionPool::GetInstance();
  _connPool->init("localhost",3306,_user,_password,_dbName,_closeLog,_sqlNum);

  _httpConns[0].initUserPasswordMap(_connPool);  // 初始化数据库，读取用户密码表格
}

void WebServer::logWrite() {
  if (_closeLog) {
    return;
  }
  if (_asyncLog) {  // 初始化日志
    Log::getInstance()->init("./ServerLog", _closeLog, 2000, 800000,
                             800);  // 异步日志
  } else {
    Log::getInstance()->init("./ServerLog", _closeLog, 2000, 800000,
                             0);  // 同步日志
  }
}

void WebServer::trigMode() {
  // LT + LT
  if (0 == _trigMode) {
    _listenFd = 0;
    _connTrigMode = 0;
  }

  // LT+ET
  if (1 == _trigMode) {
    _listenFd = 0;
    _connTrigMode = 1;
  }

  // ET+LT
  if (2 == _trigMode) {
    _listenFd = 1;
    _connTrigMode = 0;
  }

  // ET+ET
  if (3 == _trigMode) {
    _listenFd = 1;
    _connTrigMode = 1;
  }
}

void WebServer::eventListen() {
  // 网络socket通信步骤
  _listenFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  assert(_listenFd >= 0);
  struct linger tmp {
    0, 1
  };                 // 默认是优雅退出
  if (_optLinger) {  // 非优雅退出
    tmp.l_onoff = 1;
  }
  setsockopt(_listenFd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

  struct sockaddr_in serverAddr;
  bzero(&serverAddr, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(_port);

  // 在bind之前调用，为了使服务器重新启动能够绑定到这个端口上
  // 因为这个端口在服务器宕机后，可能还没有释放。如果没设置，服务器重新启动
  // 就是绑定失败，那么http就可能连接失败
  int flag = 1;
  setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

  int ret = bind(_listenFd, (const sockaddr *)&serverAddr, sizeof(serverAddr));
  assert(ret >= 0);

  ret = listen(_listenFd, 5);
  assert(ret >= 0);

  _utils.init(TIMESLOT);

  // 常见内核时间表，epoll_create
  epoll_event events[MAX_EVENT_NUMBER];
  _epollFd = epoll_create(
      5);  // 首先创建一个struct
           // eventpoll对象；然后socket注册_epollFd，监听socket读写事件
  assert(_epollFd != -1);

  // 然后分配一个未使用的文件描述符
  _utils.addFd(_epollFd, _listenFd, false, _listenTrigMode);
  HttpConn::_epollFd = _epollFd;

  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, _pipeFd);
  assert(ret != -1);
  _utils.setNonBlocking(_pipeFd[1]);             // 写，不阻塞
  _utils.addFd(_epollFd, _pipeFd[0], false, 0);  // LT，多次提醒

  _utils.addSig(SIGPIPE, SIG_IGN);
  _utils.addSig(SIGALRM, _utils.sigHandler, false);
  _utils.addSig(SIGTERM, _utils.sigHandler, false);

  alarm(TIMESLOT);

  // 工具类，信号和描述符基础操作
  Utils::_pipeFd = _pipeFd;
  Utils::_epollFd = _epollFd;
}

void WebServer::timer(int connFd, struct sockaddr_in clientAddress) {
  _httpConns[connFd].init(connFd, clientAddress, _root, _connTrigMode,
                          _closeLog, _user, _password, _dbName);

  // 初始化ClientData数据
  // 创建定时器，设置回调函数和超时事件，绑定用户数据，将定时器添加到链表中
  _userTimer[connFd]._sockFd = connFd;
  _userTimer[connFd]._address = clientAddress;

  UtilTimer *timer = new UtilTimer;
  timer->_userData = &_userTimer[connFd];
  timer->_callbackFun = callbackFunc;
  timer->_expire = time(nullptr) + 3 * TIMESLOT;

  _userTimer[connFd]._timer = timer;
  _utils._timerList.addTimer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位；并对新的定时器在链表上的位置进行调整
void WebServer::adjustTimer(UtilTimer *timer) {
  timer->_expire = time(nullptr) + 3 * TIMESLOT;
  _utils._timerList.adjustTimer(timer);
  LOG_INFO("%s", "adjust timer once");
}

void WebServer::dealTimer(UtilTimer *timer, int sockFd) {
  timer->_callbackFun(&_userTimer[sockFd]);
  if (timer) {
    _utils._timerList.delTimer(timer);
  }
  LOG_INFO("close fd %d", _userTimer[sockFd]._sockFd);
}

// 监听数据
bool WebServer::dealClientData() {
  int connFd;
  struct sockaddr_in clientAddress;
  socklen_t len = sizeof(clientAddress);
  if (0 == _listenTrigMode) {  // LT
    connFd = accept(_listenFd, (sockaddr *)&clientAddress, &len);
    if (connFd<0){
      LOG_ERROR("%s:errno is:%d","accept error",errno);
      return false;
    }
    if(HttpConn::_userCount>=MAX_FD){
      _utils.showError(connFd,"Internal server busy");
      LOG_ERROR("%s","Internal server busy");
      return false;
    }
    timer(connFd,clientAddress);
  } else {  // ET
    for (;;) {
      connFd = accept(_listenFd, (sockaddr *)(&clientAddress), &len);
      if (connFd < 0) {
        LOG_ERROR("%s:errno is:%d", "accept error", errno);
        break;
      }
      if (HttpConn::_userCount >= MAX_FD) {
        _utils.showError(connFd, "Internal server busy");
        LOG_ERROR("%s", "Internal server busy");
        break;
      }
      timer(connFd, clientAddress);
    }
    return false;
  }
  return true;
}

bool WebServer::dealWithSignal(bool &timeOut, bool &stopServer) {
  int sig;
  char signals[1024]={0};
  int ret=recv(_pipeFd[1],signals,sizeof(signals),0);
  if(ret==-1){
    return false;
  } else if(ret==0){
    return false;
  } else {
    for(int i=0;i<ret;i++){
      switch(signals[i]){
        case SIGALRM:
        {
          timeOut=true;
          break;
        }
        case SIGTERM:
        {
          stopServer=true;
          break;
        }
        default:
          break;
      }
    }
  }
  return true;
}

void WebServer::dealWithRead(int sockFd) {
  UtilTimer *timer=_userTimer[sockFd]._timer;

  if(1==_actorModel){//reactor
    if(timer){
      adjustTimer(timer);
    }
    //若检测到读事件，则将该事件放入请求队列
    _threadPool->enqueue(_httpConns+sockFd,0);//0表示读，1表示写
    for(;;){
      if(1==_httpConns[sockFd]._improve){
        if(1==_httpConns[sockFd]._timerFlag){//如果还有定时器，表示这个sockFd连接还没有关闭
          dealTimer(timer,sockFd);
          _httpConns[sockFd]._timerFlag=0;
        }
        _httpConns[sockFd]._improve=0;
        break;
      }
    }
  } else {
    //proactor
    if(_httpConns[sockFd].readOnce()){//
      LOG_INFO("deal with the client(%s)",inet_ntoa(_httpConns[sockFd].getAddress()->sin_addr));
      _threadPool->enqueue(_httpConns+sockFd);//如果检测到该读事件，将该事件放入请求队列
      if(timer){
        adjustTimer(timer);
      }
    } else {
      dealTimer(timer,sockFd);//如果读取失败，就删除定时器，不能成功接收信息
    }
  }
}

void WebServer::dealWithWrite(int sockFd) {
  UtilTimer *timer=_userTimer[sockFd]._timer;

  if(1==_actorModel){//reactor
    if(timer){
      adjustTimer(timer);
    }

    _threadPool->enqueue(_httpConns+sockFd,1);
    for(;;){
      if(1==_httpConns[sockFd]._improve){
        if(1==_httpConns[sockFd]._timerFlag){
          dealTimer(timer,sockFd);
          _httpConns[sockFd]._timerFlag=0;
        }
        _httpConns[sockFd]._improve=0;
        break;
      }
    }
  } else {
    //proactor
    if(_httpConns[sockFd].write()){
      LOG_INFO("send data to the client(%s)",inet_ntoa(_httpConns[sockFd].getAddress()->sin_addr));
      if(timer){
        adjustTimer(timer);
      }
    } else {
      dealTimer(timer,sockFd);//写入失败，关闭连接
    }
  }
}

void WebServer::eventLoop() {
  bool timeOut=false;
  bool stopServer=false;
  while(!stopServer){
    int number=epoll_wait(_epollFd,_events,MAX_EVENT_NUMBER,-1);
    if(number<0&&errno!=EINTR){
      LOG_ERROR("%s","epoll failure");
      break;
    }

    for(int i=0;i<number;i++){
      int sockFd=_events[i].data.fd;
      if(sockFd==_listenFd){//如果等于服务器套接字，表示有事件激活了服务器，accept事件
        bool flag=dealClientData();
        if(!flag){
          continue;
        }
      } else if(_events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){//EPOLLRDHUP：客户端关闭，EPOLLHUP：客户端挂起，EPOLLERR：错误
        //EPOLLHUP：它表示描述符的一端或两端已经关闭或挂断，或者被其他错误关闭
        dealTimer((_userTimer+sockFd)->_timer,sockFd);
      } else if(sockFd==_pipeFd[0]&&(_events[i].events&(EPOLLIN))){//处理信号，管道通信
        bool flag=dealWithSignal(timeOut,stopServer);
        if(!flag){
          LOG_ERROR("%s","deal client data failure");
        }
      } else if(_events[i].events&EPOLLIN){//处理客户连接上接收到的数据
        dealWithRead(sockFd);
      } else if(_events[i].events&EPOLLOUT){//写数据，发送给客户端
        dealWithWrite(sockFd);
      }
    }

    if(timeOut){
      _utils.timerHandler();
      LOG_INFO("%s","timer tick");
      timeOut=false;
    }
  }
}
