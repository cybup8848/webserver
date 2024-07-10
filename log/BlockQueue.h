#ifndef _BLOKC_QUEUE_H_
#define _BLOCK_QUEUE_H_
#include<mutex>
#include<condition_variable>
#include<chrono>

//循环队列，尾部插入，头部弹出
template<typename T>
class BlockQueue{
  public:
    BlockQueue(int size=1000);
    ~BlockQueue();
    void clear();

    bool full();
    bool empty();

    T& front();
    
    T& back();

    int size();
    int maxSize();

    void push(const T&item);
    void pop(T &item);
    void pop(T &item,int msTimeOut);

  private:
    std::mutex _queueMutex;
    std::condition_variable _condition;
    
    T *_array;
    int _size;
    int _maxSize;

    int _front;
    int _back;
};

template<typename T>
BlockQueue<T>::BlockQueue(int size){
  if(size<=0){
    exit(-1);
  }
  _array=new T[size];
  _maxSize=size;
  _size=0;
  _front=-1;
  _back=-1;
}

template<typename T>
BlockQueue<T>::~BlockQueue(){
  std::unique_lock<std::mutex> lock(_queueMutex);
  if(_array){
    delete [] _array;
    _array=nullptr;
  }
}

template<typename T>
void BlockQueue<T>::clear(){
  std::unique_lock<std::mutex> lock(_queueMutex);
  _size=0;
  _front=-1;
  _back=-1;
}

template<typename T>
bool BlockQueue<T>::full(){
  std::unique_lock<std::mutex> lock(_queueMutex);
  return _size==_maxSize;
}

template<typename T>
bool BlockQueue<T>::empty(){
  std::unique_lock<std::mutex> lock(_queueMutex);
  return _size==0;
}

template<typename T>
T& BlockQueue<T>::front(){
  std::unique_lock<std::mutex> lock(_queueMutex);
  assert(!empty());
  return _array[_front];
}
    
template<typename T>
T& BlockQueue<T>::back(){
  std::unique_lock<std::mutex> lock(_queueMutex);
  assert(!empty());
  return _array[_back];
}

template<typename T>
int BlockQueue<T>::size(){
  int tmp=0;
  {
    std::unique_lock<std::mutex> lock(_queueMutex);
    tmp=_size;
  }
  return tmp;
}

template<typename T>
int BlockQueue<T>::maxSize(){
  int tmp=0;
  {
    std::unique_lock<std::mutex> lock(_queueMutex);
    tmp=_maxSize;
  }
  return tmp;
}

template<typename T>
void BlockQueue<T>::push(const T&item){
  _condition.notify_all();
  {
    std::unique_lock<std::mutex> lock(_queueMutex);
    _condition.wait(lock,[this]{return !this->full();});
    _size+=1;
    _back=(_back+1)%_maxSize;
    _array[_back]=item;
  }
  _condition.notify_one();
}

template<typename T>
void BlockQueue<T>::pop(T &item){
  std::unique_lock<std::mutex> lock;
  _condition.wait(lock,[this]{return !this->empty();});
  item=_array[_front];
  _size-=1;
  _front=(_front+1)%_maxSize;
}

template<typename T>
void BlockQueue<T>::pop(T &item,int msTimeOut){
  std::unique_lock<std::mutex> lock;
  _condition.wait_for(lock,std::chrono::milliseconds(msTimeOut),[this]{return !this->empty();});
  if(_size<=0){
    return;
  }
  item=_array[_front];
  _size-=1;
  _front=(_front+1)%_maxSize;
}
#endif


