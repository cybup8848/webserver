#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_
#include<vector>
#include<queue>
#include<thread>
#include<mutex>
#include<condition_variable>
#include"../CGImysql/SqlConnectionPool.h"

template<typename T>
class ThreadPool{
  public:
    ThreadPool(int actorModel,ConnectionPool *connPool,int threadNumber=8,int maxRequests=10000);
    ~ThreadPool();

    void enqueue(T *request,int state);//_state：0读，1写
    void enqueue(T *request);

  private:
    void run();

  private:
    int _threadNumber;//线程池中的线程个数
    std::vector<std::thread> _workers;//工作线程

    int _maxRequests;//最大请求数
    std::queue<T *> _requestTasks;//请求任务

    std::mutex _requestMutex;//请求互斥量
    std::condition_variable _condition;

    ConnectionPool *_connPool;//数据库连接池

    int _actorModel;//模型切换

    bool _stop;
};

template<typename T>
ThreadPool<T>::ThreadPool(int actorModel,ConnectionPool *connPool,int threadNumber,int maxRequests){
  _stop=false;
  _actorModel=actorModel;
  _connPool=connPool;
  _threadNumber=threadNumber;
  _maxRequests=maxRequests;

  if(threadNumber<=0||maxRequests<=0){
    throw std::exception();
  }

  for(int i=0;i<threadNumber;i++){
    _workers.emplace_back([this](){
      for(;;){
        std::unique_lock<std::mutex> lock(this->_requestMutex);
        this->_condition.wait(lock,[this]{return this->_stop||!this->_requestTasks.empty();});
        if(this->_stop&&this->_requestTasks.empty()){
          return;
        }
        run();
      }
    });
  }
}

template<typename T>
ThreadPool<T>::~ThreadPool(){
  {
    std::unique_lock<std::mutex> lock(_requestMutex);
    _stop=true;
  }
  _condition.notify_all();
  for(std::thread &th:_workers){
      th.join();
  }
}

template<typename T>
void ThreadPool<T>::enqueue(T *request,int state){
  std::unique_lock<std::mutex> lock(_requestMutex);
  if(_requestTasks.size()>=_maxRequests){
    return;
  }

  request->_state=state;
  _requestTasks.emplace(request);
  _condition.notify_one();
}

template<typename T>
void ThreadPool<T>::enqueue(T *request){
  std::unique_lock<std::mutex> lock(_requestMutex);
  if(_requestTasks.size()>=_maxRequests){
    return;
  }

  _requestTasks.emplace(request);
  _condition.notify_one();
}

template<typename T>
void ThreadPool<T>::run(){
  T *request=std::move(_requestTasks.front());
  _requestTasks.pop();
  if(!request){
    return;
  }

  if(1==_actorModel){

    if(0==request->_state){//_state：0读，1写
      if(request->readOnce()){
        request->_improve=1;
        ConnectionRAII mysqlCon(request->_mysql,_connPool);
        request->process();
      } else {
        request->_improve=1;
        request->_timerFlag=1;
      }
    } else {//写
      if(request->write()){
        request->_improve=1;
      } else {
        request->_improve=1;
        request->_timerFlag=1;
      }
    }

  } else {
    ConnectionRAII mysqlCon(request->_mysql,_connPool);
    request->process();
  }
}
#endif