#include"HttpConn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

std::mutex _mtx;

int HttpConn::_userCount=0;
int HttpConn::_epollFd=-1;

std::unordered_map<std::string,std::string> _mapUserPassword;

//对描述符设置非阻塞
int setNonBlocking(int fd){
  int oldOption=fcntl(fd,F_GETFL,0);
  fcntl(fd,F_SETFL,oldOption|O_NONBLOCK);
  return oldOption;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addFd(int epollFd,int fd,bool oneShot,int trigMode){
  epoll_event events;
  events.data.fd=fd;
  events.events=EPOLLIN|EPOLLRDHUP;
  if(1==trigMode){//1：ET，0：LT
    events.events|=EPOLLET;
  }

  if(oneShot){
    events.events|=EPOLLONESHOT;
  }

  epoll_ctl(epollFd,EPOLL_CTL_ADD,fd,&events);
  setNonBlocking(fd);
}

//从内核时间表删除描述符
void removeFd(int epollFd,int fd){
  epoll_ctl(epollFd,EPOLL_CTL_DEL,fd,nullptr);
  close(fd);
}

//将事件重置为EPOLLONESHOT
void modFd(int epollFd,int fd,int ev,int trigMode){
  epoll_event events;
  events.data.fd=fd;
  events.events=ev|EPOLLONESHOT|EPOLLRDHUP;
  if(1==trigMode){
    events.events|=EPOLLET;
  }
  epoll_ctl(epollFd,EPOLL_CTL_MOD,fd,&events);
}

//public:
//初始化连接，外部调用初始化套接字地址
void HttpConn::init(int sockFd,const struct sockaddr_in &addr,char *root,int trigMode,bool closeLog,std::string user,std::string password,std::string dbName){
  _sockFd=sockFd;
  _address=addr;
  _docRoot=root;
  _trigMode=trigMode;
  _closeLog=closeLog;
  _sqlUser=user;
  _sqlPassword=password;
  _dbName=dbName;

  addFd(_epollFd,sockFd,true,trigMode);
  HttpConn::_userCount++;
  init();
}

void HttpConn::init(){
  _readIndex=0;
  _writeIndex=0;
  _checkIndex=0;

  _startLine=0;

  _mysql=nullptr;
  _bytes_to_send=0;
  _bytes_have_send=0;
  _checkState=CHECK_STATE_REQUESTLINE;
  

  _method=GET;
  _url=nullptr;
  _version=nullptr;
  _host=nullptr;
  _bodyLength=0;
  _linger=false;//是否优雅退出
  _fileAddress=nullptr;

  _ivCount=0;
  _cgi=false;
  _requestBody=nullptr;

  memset(_readBuf,'\0',READ_BUFFER_SIZE);
  memset(_writeBuf,'\0',WRITE_BUFFER_SIZE);
  memset(_realFile,'\0',FILENAME_LEN);
}

void HttpConn::closeConn(bool realClose){//关闭连接
  if(realClose&&_sockFd!=-1){
    std::cout<<"close "<<_sockFd<<std::endl;
    removeFd(_epollFd,_sockFd);
    HttpConn::_userCount--;
    _sockFd=-1;
  }
}

struct sockaddr_in* HttpConn::getAddress(){
  return &_address;
}

void HttpConn::initUserPasswordMap(ConnectionPool *connPool){
  MYSQL *mysql=nullptr;
  ConnectionRAII connRAII(mysql,connPool);
  if(mysql_query(mysql,"select username,password from users")){
    LOG_ERROR("SELECT error:%s",mysql_error(mysql));
  }

  MYSQL_RES *result=mysql_store_result(mysql);
  MYSQL_ROW row;
  while(row=mysql_fetch_row(result)){
    _mapUserPassword[row[0]]=row[1];
  }
  mysql_free_result(result);
}

//对readOnce读取浏览器端发送来的请求报文，直到无数据可读或对方关闭连接，
//读取到_readBuf中，并更新_readIndex
//只是阻塞模式下recv会阻塞着接收数据，非阻塞模式下如果没有数据会返回，不会阻塞着读，因此需要循环读取）。
//http的请求报文和响应报文都是通过socket传输的，这里读取的就是请求、响应报文
bool HttpConn::readOnce(){
  if(_readIndex>=READ_BUFFER_SIZE){
    return false;
  }
  int readBytes=0;

  if(0==_trigMode){//0代表LT模式，不需要一次性读完，阻塞
    readBytes=recv(_sockFd,_readBuf+_readIndex,
      READ_BUFFER_SIZE-_readIndex,0);
    if(readBytes<=0){
      return false;
    }
    readBytes+=readBytes;
    return true;
  }

  for(;;){//ET模式，非阻塞
    readBytes=recv(_sockFd,_readBuf+_readIndex,
      READ_BUFFER_SIZE-_readIndex,0);

    //返回值<0时并且
    //(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)的
    //情况下认为连接是正常的，继续接收。
    if(readBytes==-1){
      //非阻塞ET模式下，需要一次性将数据读完
      if(errno==EAGAIN||errno==EWOULDBLOCK){
        break;
      }
      return false;
    } else if(readBytes==0){//连接关闭
      return false;
    }
    _readIndex+=readBytes;//修改_readIndex的读取字节数
  }
  return true;
}



//KMP算法
int* getNext(const char *str){
  if(str==nullptr){
    return nullptr;
  }
  int len=strlen(str);
  if(len==0){
    return nullptr;
  }

  int *next=new int[len];
  next[0]=-1;
  if(len>=2){
    next[1]=0;
  }

  int count=0;
  int index=2;
  while(index<len){
    if(str[index-1]==str[count]){
      count++;
      index++;
    } else if(next[count]>0){
      count=next[count];
    } else {
      count=0;
      next[index++]=count;
    }
  }
  return next;
}

int getPos(const char *str1,const char *str2){
  if(str1==nullptr||str2==nullptr){
    return -1;
  }
  int len1=strlen(str1);
  int len2=strlen(str2);
  if(len1==0||len2==0){
    return -1;
  }
  int *next=getNext(str2);
  int i=0;
  int j=0;
  while(i<len1){
    if(str1[i]==str2[j]){
      i++;
      j++;
    } else if(next[j]>=0){
      j=next[i];
    } else {
      i++;
      j=0;
    }
  }
  return j==len2?i-j:-1;
}

//http请求报文由三部分组成：请求行、请求头、请求体
HttpConn::HTTP_CODE HttpConn::parseRequestLine(char *text){//主状态机解析报文中的请求行数据
  _url=strpbrk(text," \t");
  if(!_url){
    return NO_REQUEST;
  }
  *_url='\0';
  if(strcasecmp(text,"GET")==0){
    _method=GET;
  } else if(strcasecmp(text,"POST")==0){
    _cgi=true;
    _method=POST;
  } else {
    return BAD_REQUEST;
  }

  _url+=strspn(_url," \t");
  _version=strpbrk(_url," \t");
  if(!_version){
    return BAD_REQUEST;
  }
  *_version++='\0';
  _version+=strspn(_version," \t");
  if(strcasecmp(_version,"HTTP/1.1")!=0){
    return BAD_REQUEST;
  }

  if(strncasecmp(_url,"http://",7)==0){
    _url+=7;
    
  }
  if(strncasecmp(_url,"https://",8)==0){
    _url+=8;
  }
  _url=strchr(_url,'/');
  if(!_url){
    return BAD_REQUEST;
  }

  if(strlen(_url)==1){
    strcat(_url,"judge.html");//仅仅有一个'/'
  }
  _checkState=CHECK_STATE_REQUESTHEADER;
  return NO_REQUEST;//请求不完整，继续接收
}

//char *_host;
//long int _bodyLength;
//bool _linger;//是否保持长连接，连接是否复用
HttpConn::HTTP_CODE HttpConn::parseRequestHeader(char *text){//主状态机解析报文中的请求头数据
  if(text[0]=='\0'){
    if(_bodyLength!=0){
      _checkState=CHECK_STATE_REQUESTBODY;
      return NO_REQUEST;
    }
    return GET_REQUEST;
  } else if(strncasecmp(text,"Connection:",11)==0){
    text+=11;
    text+=strspn(text," \t");
    if(strcasecmp(text,"keep-alive")==0){
      _linger=true;
    }
  } else if(strncasecmp(text,"Host:",5)==0){
    text+=5;
    text+=strspn(text," \t");
    _host=text;
  } else if(strncasecmp(text,"Content-length:",15)==0){
    text+=15;
    text+=strspn(text," \t");
    _bodyLength=std::atol(text);
  } else {
    LOG_INFO("unordered headers info:%s",text);
  }
  return NO_REQUEST;
}

//判断http请求是否被完整读入
HttpConn::HTTP_CODE HttpConn::parseRequestBody(char *text){//主状态机解析报文中的请求体内容
  if(_readIndex>=(_bodyLength+_checkIndex)){//判断_readBuf中是否读取了消息体
    text[_bodyLength]='\0';
    _requestBody=text;//POST请求中最后为输入的用户名和密码
    return GET_REQUEST;
  }
  return NO_REQUEST;
}

//_startLine是已经解析的字符，getLine用于将指针向后偏移，指向未处理的字符   
char* HttpConn::getLine(){
  return _readBuf+_startLine;
}

//从状态机读取一行，分析是请求报文的哪一部分
HttpConn::LINE_STATUS HttpConn::parseLine(){
  for(;_checkIndex<_readIndex;++_checkIndex){
    if(_readBuf[_checkIndex]=='\r'){
      if(_checkIndex+1==_readIndex){
        return LINE_OPEN;
      } else if(_readBuf[_checkIndex+1]=='\n'){
        _readBuf[_checkIndex++]='\0';
        _readBuf[_checkIndex++]='\0';
        return LINE_OK; 
      }
      return LINE_BAD;
    } else if(_readBuf[_checkIndex]=='\n'){
      if(_checkIndex>1&&_readBuf[_checkIndex-1]=='\r'){
        _readBuf[_checkIndex-1]='\0';
        _readBuf[_checkIndex++]='\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  return LINE_OPEN;
}

HttpConn::HTTP_CODE HttpConn::processRead(){//从_readBuf读取，并处理请求报文  
  LINE_STATUS lineStatus=LINE_OK;
  HTTP_CODE ret=NO_REQUEST;
  char *text=nullptr;
  while((_checkState==CHECK_STATE_REQUESTBODY&&lineStatus==LINE_OK)||((lineStatus=parseLine())==LINE_OK)){
    text=getLine();
    _startLine=_checkIndex;
    LOG_INFO("%s",text);
    switch(_checkState){
    case CHECK_STATE_REQUESTLINE:
    {
      ret=parseRequestLine(text);
      if(ret==BAD_REQUEST){
        return BAD_REQUEST;
      }
      break;
    }
    case CHECK_STATE_REQUESTHEADER:
    {
      ret=parseRequestHeader(text);
      if(ret==BAD_REQUEST){
        return BAD_REQUEST;
      } else if(ret==GET_REQUEST){//get方式，请求体为空（这里可以已经处理好http请求报文）；post方式，请求体不为空
        return doRequest();
      }
      break;
    }
    case CHECK_STATE_REQUESTBODY:
    {
      ret=parseRequestBody(text);
      if(ret==GET_REQUEST){
        return doRequest();
      }
      lineStatus=LINE_OPEN;//解析完消息体即完成报文解析，避免再次进入循环，更新line_status
      break;
    }
    default:
      return INTERNAL_ERROR;
    }
  }
  return NO_REQUEST;
}

//process_read函数的返回值是对请求的文件分析后的结果，一部分是语法错误导致的BAD_REQUEST，一部分是do_request的返回结果.
//该函数将网站根目录和url文件拼接，然后通过stat判断该文件属性。另外，为了提高访问速度，通过mmap进行映射，将普通文件映射到内存逻辑地址。

//为了更好的理解请求资源的访问流程，这里对各种各页面跳转机制进行简要介绍。其中，浏览器网址栏中的字符，即url，可以将其抽象成ip:port/xxx，
//xxx通过html文件的action属性进行设置。

//m_url为请求报文中解析出的请求资源，以/开头，也就是/xxx，项目中解析后的m_url有8种情况。
// /：GET请求，跳转到judge.html，即欢迎访问页面
// /0：POST请求，跳转到register.html，即注册页面
// /1：POST请求，跳转到log.html，即登录页面
// /2CGISQL.cgi
//    POST请求，进行登录校验
//    验证成功跳转到welcome.html，即资源请求成功页面
//    验证失败跳转到logError.html，即登录失败页面
// /3CGISQL.cgi
//    POST请求，进行注册校验
//    注册成功跳转到log.html，即登录页面
//    注册失败跳转到registerError.html，即注册失败页面
// /5：POST请求，跳转到picture.html，即图片请求页面
// /6：POST请求，跳转到video.html，即视频请求页面
// /7：POST请求，跳转到fans.html，即关注页面

HttpConn::HTTP_CODE HttpConn::doRequest(){//生成响应报文
  strcpy(_realFile,_docRoot);
  int len=strlen(_docRoot);
  const char *p=strrchr(_url,'/');//查找'/'在字符串_url中最后一次出现的位置

  //处理cgi
  if((_cgi==1)&&(*(p+1)=='2'||*(p+1)=='3')){
    //将用户名和密码提取出来
    //user=123&passwd=123
    char userName[100]={0};
    char password[100]={0};
    const char *firstEqual=strchr(_requestBody,'=');
    const char *andCh=strchr(firstEqual,'&');
    const char *secondEqual=strchr(andCh,'=');
    strncpy(userName,firstEqual+1,andCh-firstEqual-1);
    strcpy(password,secondEqual+1);

    ////根据标志判断是登录检测还是注册检测
    if(*(p+1)=='3'){//如果是注册，先检车数据库中是否有重名的；没有崇明，进行增加数据
      char sqlInsert[200]={0};
      sprintf(sqlInsert,"insert into user(username,password) value('%s','%s')",userName,password);
      strcpy(_url,"/registerError.html");//这里觉得有问题，因为_url不知道多长
      if(_mapUserPassword.find(userName)==_mapUserPassword.end()){
        int res=0;
        {
          std::unique_lock<std::mutex> lock(_mtx);
          res=mysql_query(_mysql,sqlInsert);
        }
        if(!res){//注册成功
          {
            std::unique_lock<std::mutex> lock(_mtx);
            _mapUserPassword[userName]=password;
          }
          strcpy(_url,"/log.html");
        }
      }
    } else if(*(p+1)=='2'){//如果是登录，直接判断；若浏览器输入的用户名和密码在表中可以查找到，返回1，否则返回0
      strcpy(_url,"/logError.html");
      if(_mapUserPassword.find(userName)!=_mapUserPassword.end()&&_mapUserPassword[userName]==password){
        strcpy(_url,"/welcome.html");
      }
    }
  }

  switch(*(p+1)){
    case '0':
    {
      strcpy(_url,"/register.html");
      break;
    }
    case '1':
    {
      strcpy(_url,"/log.html");
      break;
    }
    case '5':
    {
      strcpy(_url,"/picture.html");
      break;
    }
    case '6':
    {
      strcpy(_url,"/video.html");
      break;
    }
    case '7':
    {
      strcpy(_url,"/fans.html");
      break;
    }
    default:
      ;
  }
  strncat(_realFile+len,_url,strlen(_url));
  
  if(stat(_realFile,&_fileStat)<0){
    return NO_REQUEST;
  }
  if(!_fileStat.st_mode&S_IROTH){
    return FORBIDDEN_REQUEST;
  }
  if(S_ISDIR(_fileStat.st_mode)){
    return BAD_REQUEST;
  }

  int fd=open(_realFile,O_RDONLY);
  _fileAddress=(char *)mmap(nullptr,_fileStat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
  close(fd);
  return FILE_REQUEST;
}

void HttpConn::unmap(){
  if(_fileAddress){
    munmap(_fileAddress,_fileStat.st_size);
    _fileAddress=nullptr;
    bzero(&_fileStat,sizeof(struct stat));
  }
}

//服务器子线程调用process_write完成响应报文，随后注册epollout事件。服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端。

//该函数具体逻辑如下：
//在生成响应报文时初始化byte_to_send，包括头部信息和文件数据大小。通过writev函数循环发送响应报文数据，根据返回值更新byte_have_send和iovec结构体的指针和长度，
//并判断响应报文整体是否发送成功。
//若writev单次发送成功，更新byte_to_send和byte_have_send的大小，若响应报文整体发送成功,则取消mmap映射,并判断是否是长连接.
//长连接重置http类实例，注册读事件，不关闭连接；短连接直接关闭连接

//若writev单次发送不成功，判断是否是写缓冲区满了。
//若不是因为缓冲区满了而失败，取消mmap映射，关闭连接
//若eagain则满了，更新iovec结构体的指针和长度，并注册写事件，等待下一次写事件触发（当写缓冲区从不可写变为可写，触发epollout），
//因此在此期间无法立即接收到同一用户的下一请求，但可以保证连接的完整性。

bool HttpConn::write(){// 响应报文写入函数 
  int temp=0;
  if(_bytes_to_send==0){//若要发送的数据长度为0，表示响应报文为空，一般不会出现这种情况
    modFd(_epollFd,_sockFd,EPOLLIN,_trigMode);
    init();
    return true;
  }
  
  while(true){
    //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
    temp=writev(_sockFd,_iv,_ivCount);//函数调用成功时返回读、写的总字节数，失败时返回-1并设置相应的errno。
    if(temp==-1){
      if(errno==EAGAIN){//缓冲区满，所以写失败
        modFd(_epollFd,_sockFd,EPOLLOUT,_trigMode);//写入失败，继续监听写事件
        return true;
      }
      unmap();
      return false;
    } 

    _bytes_have_send+=temp;
    _bytes_to_send-=temp;
    if(_bytes_have_send>=_iv[0].iov_len){
      _iv[0].iov_len=0;
      _iv[1].iov_base=_fileAddress+(_bytes_have_send-_writeIndex);
      _iv[1].iov_len=_bytes_to_send;
    } else {
      _iv[0].iov_base=_writeBuf+_bytes_have_send;
      _iv[0].iov_len-=_bytes_have_send;
    }

    if(_bytes_to_send<=0){//发送完成
      unmap();
      modFd(_epollFd,_sockFd,EPOLLIN,_trigMode);//继续监听读事件
      if(_linger){//是否保持长连接，发送完就close，还是kepp-alive
        init();
        return true;
      }
      return false;
    }
  }
  return true;
}

//http响应报文也由三部分组成：响应行、响应头、响应体
//根据响应报文格式，生成对应8个部分，以下函数均有doRequest调用
bool HttpConn::addResponse(const char *format,...){
  if(_writeIndex>=WRITE_BUFFER_SIZE){
    return false;
  }
  va_list args;
  va_start(args,format);
  int len=snprintf(_writeBuf+_writeIndex,WRITE_BUFFER_SIZE-_writeIndex-1,format,args);
  if(len>=WRITE_BUFFER_SIZE-_writeIndex-1){//写入完成
    va_end(args);
    return true;
  }
  _writeIndex+=len;
  va_end(args);
  LOG_INFO("request:%s",_writeBuf);
  return true;
}

bool HttpConn::addResponseLine(int status,const char *title){
  return addResponse("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool HttpConn::addResponseHeader(int bodyLength){
  return addResponseBodyLength(bodyLength)&addLinger()&addBlankLine();
}
      
bool HttpConn::addResponseBodyType(){
  return addResponse("Content-Type:%s\r\n", "text/html");
}

bool HttpConn::addResponseBodyLength(int bodyLength){
  return addResponse("Content-Length:%d\r\n",bodyLength);
}

bool HttpConn::addLinger(){
  return addResponse("Connection:%s\r\n",_linger?"keep-alive":"close");
}

bool HttpConn::addBlankLine(){
  return addResponse("\r\n");
}

bool HttpConn::addResponseBody(const char *body){
  return addResponse("%s",body);
}

bool HttpConn::processWrite(HTTP_CODE ret){//向_writeBuf写入响应报文数据
  switch(ret){
    case INTERNAL_ERROR:
    {
      addResponseLine(500,error_500_title);
      addResponseHeader(strlen(error_500_form));
      if(!addResponseBody(error_500_form)){
        return false;
      }
      break;
    }
    case BAD_REQUEST:
    {
      addResponseLine(404,error_404_title);
      addResponseHeader(strlen(error_404_form));
      if(!addResponseBody(error_404_form)){
        return false;
      }
      break;
    }
    case FORBIDDEN_REQUEST:
    {
      addResponseLine(403,error_403_title);
      addResponseHeader(strlen(error_403_form));
      if(!addResponseBody(error_403_form)){
        return false;
      }
      break;
    }
    case FILE_REQUEST:
    {
      addResponseLine(200,ok_200_title);
      if(_fileStat.st_size!=0){
        addResponseHeader(_fileStat.st_size);//这是响应行、响应头已经写完成
        _iv[0].iov_base=_writeBuf;
        _iv[0].iov_len=_writeIndex;
        _iv[1].iov_base=_fileAddress;//响应体地址
        _iv[1].iov_len=_fileStat.st_size;
        _ivCount=2;
        _bytes_to_send=_writeIndex+_fileStat.st_size;
        return true;
      } else {
        const char *okString="<html><body></body></html>";
        addResponseHeader(strlen(okString));
        if(!addResponseBody(okString)){
          return false;
        }
      }
    }
    default:
      return false;
  }

  _iv[0].iov_base=_writeBuf;
  _iv[0].iov_len=_writeIndex;
  _ivCount=1;
  _bytes_to_send=_writeIndex;
  return true;
}

//各子线程通过process函数对任务进行处理，调用process_read函数
//和process_write函数分别完成报文解析与报文响应两个任务
void HttpConn::process(){
  HTTP_CODE readRet=processRead();//报文解析结果
  if(NO_REQUEST==readRet){
    modFd(_epollFd,_sockFd,EPOLLIN,_trigMode);//NO_REQUEST：表示请求不完整，需要继续接收请求数据
    return;
  }

  bool writeRet=processWrite(readRet);
  if(!writeRet){
    closeConn();
  }
  modFd(_epollFd,_sockFd,EPOLLOUT,_trigMode);
}




