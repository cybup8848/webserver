#include "lst_timer.h"
SortTimerList::SortTimerList() { _head = _tail = nullptr; }

SortTimerList::~SortTimerList() {
  UtilTimer *tmp = nullptr;
  while (_head) {
    tmp = _head->_next;
    delete _head;
    _head = tmp;
  }
}

void SortTimerList::addTimer(UtilTimer *timer) {
  if (!timer) {
    return;
  }

  if (!_head) {
    timer->_prev = timer->_next = nullptr;
    _head = _tail = timer;
    return;
  }

  UtilTimer *prev = nullptr;
  UtilTimer *cur = _head;
  while (cur && timer->_expire > cur->_expire) {
    cur = cur->_next;
  }

  if (!prev) {
    timer->_prev = nullptr;
    timer->_next = cur;
    cur->_prev = timer;
    _head = timer;
  } else if (!cur) {
    prev->_next = timer;
    timer->_prev = prev;
    timer->_next = nullptr;
    _tail = timer;
  } else {
    timer->_next = cur;
    timer->_prev = prev;
    cur->_prev = timer;
    prev->_next = timer;
  }
}

void SortTimerList::adjustTimer(UtilTimer *timer) {
  if (!timer) {
    return;
  }

  UtilTimer *next = timer->_next;
  if (!next || timer->_expire < next->_expire) {
    return;
  }

  if (timer == _head) {
    _head = _head->_next;
    _head->_prev = nullptr;
  } else {
    timer->_prev->_next = timer->_next;
    timer->_next->_prev = timer->_prev;
  }
  addTimer(timer);
}

void SortTimerList::delTimer(UtilTimer *timer) {
  if (!timer) {
    return;
  }
  if (timer == _head && timer == _tail) {
    _head = _tail = nullptr;
  }
  if (timer == _head) {
    _head = _head->_next;
    _head->_prev = nullptr;
  } else if (timer == _tail) {
    _tail = _tail->_prev;
    _tail->_next = nullptr;
  } else {
    timer->_prev->_next = timer->_next;
    timer->_next->_prev = timer->_prev;
  }
  delete timer;
  timer = nullptr;
}

void SortTimerList::tick() {
  time_t nowTime = time(nullptr);
  UtilTimer *tmp = _head;
  while (tmp) {
    if (nowTime > tmp->_expire) {
      break;
    }
    _head = tmp->_next;
    tmp->_callbackFun(tmp->_userData);
    if (_head) {
      _head->_prev = nullptr;
    }
    delete tmp;
    tmp = _head;
  }
}

void Utils::init(int timeSlot) { 
  _timeSlot = timeSlot; 
}

int Utils::setNonBlocking(int fd) {  // 对文件描述符设置非阻塞
  int oldOption = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, oldOption | O_NONBLOCK);
  return oldOption;
}

// 客户端直接调用close，会触犯EPOLLRDHUP事件
// 通过EPOLLRDHUP属性，来判断是否对端已经关闭，这样可以减少一次系统调用

// 第二种方法就是本文要提到的EPOLLONESHOT这种方法，可以在epoll上注册这个事件，注册这个事件后，
// 如果在处理写成当前的SOCKET后不再重新注册相关事件，那么这个事件就不再响应了或者说触发了。
// 要想重新注册事件则需要调用epoll_ctl重置文件描述符上的事件，这样前面的socket就不会出现
// 竞态这样就可以通过手动的方式来保证同一SOCKET只能被一个线程处理，不会跨越多个线程。
void Utils::addFd(
    int epollFd, int fd, bool oneShot,
    int trigMode) {  //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT；开启监听内核事件
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLRDHUP;
  if (1 == trigMode) {  // 1代表ET，0代表LT
    event.events |= EPOLLET;
  }
  if (oneShot) {
    event.events |= EPOLLONESHOT;
  }
  epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event);
  setNonBlocking(fd);
}

void Utils::addSig(int sig, void (*handler)(int),
                   bool restart) {  // 设置信号函数
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = handler;
  if (restart) {
    sa.sa_flags |= SA_RESTART;//由此信号中断的系统调用自动重启动
  }
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, nullptr) != -1);  // 和singal(sig,handler);效果一样
}

void Utils::timerHandler() {  // 定时处理任务，重新定时以不断触发SIGALARM信号
  _timerList.tick();
  alarm(_timeSlot);
}

void Utils::showError(int connFd, const char *info) {
  send(connFd, info, strlen(info), 0);
  close(connFd);
}

void Utils::sigHandler(int sig) {  // 信号处理函数
  int saveErrno = errno;
  int msg = sig;
  send(_pipeFd[1], (char *)&msg, 1, 0);
  errno = saveErrno;
}

int *Utils::_pipeFd = nullptr;
int Utils::_epollFd = 0;

void callbackFunc(ClientData *userData) {
  assert(userData);
  epoll_ctl(Utils::_epollFd, EPOLL_CTL_DEL, userData->_sockFd, nullptr);
  close(userData->_sockFd);
  HttpConn::_userCount--;  // 结束一个http连接
}
