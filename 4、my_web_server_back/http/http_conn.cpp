// Http请求，后续主要是处理这个头
//
// GET / HTTP/1.1
// Host: 192.168.0.23:47310
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*; q = 0.8
// Accept - Encoding: gzip, deflate, sdch
// Accept - Language : zh - CN, zh; q = 0.8
// Cookie: __guid = 179317988.1576506943281708800.1510107225903.8862; monitor_count = 5
//

// POST / color1.cgi HTTP / 1.1
// Host: 192.168.0.23 : 47310
// Connection : keep - alive
// Content - Length : 10
// Cache - Control : max - age = 0
// Origin : http ://192.168.0.23:40786
// Upgrade - Insecure - Requests : 1
// User - Agent : Mozilla / 5.0 (Windows NT 6.1; WOW64) AppleWebKit / 537.36 (KHTML, like Gecko) Chrome / 55.0.2883.87 Safari / 537.36
// Content - Type : application / x - www - form - urlencoded
// Accept : text / html, application / xhtml + xml, application / xml; q = 0.9, image / webp, */*;q=0.8
// Referer: http://192.168.0.23:47310/
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-CN,zh;q=0.8
// Cookie: __guid=179317988.1576506943281708800.1510107225903.8862; monitor_count=281
// Form Data
// color=gray

/**http连接处理类：
 * 根据状态转移,通过主从状态机封装了http连接类。其中,主状态机在内部调用从状态机,从状态机将处理状态和数据传给主状态机
    > * 客户端发出http连接请求
    > * 从状态机读取数据,更新自身状态和接收数据,传给主状态机
    > * 主状态机根据从状态机状态,更新自身状态,决定响应请求还是继续读取
 * 
 ********************************************
 *
 * 一、基础知识方面，包括HTTP报文格式，状态码和有限状态机。
 * 
 * 1、HTTP报文格式
 *      HTTP报文分为请求报文(web->服务端)和响应报文(服务端->web)两种，
 *      每种报文必须按照特有格式生成，才能被浏览器端识别。
 * (1)请求报文
 *      HTTP请求报文由请求行（request line）、请求头部（header）、空行和请求数据四个部分组成。
 *      其中，请求报文也分为两种，GET和POST
 * (2)响应报文
 *      HTTP响应也由四个部分组成，分别是：状态行、消息报头、空行和响应正文。
 * 
 * 2、HTTP状态码
 * HTTP有5种类型的状态码，具体的：
 *  1xx：指示信息--表示请求已接收，继续处理。
 *  2xx：成功--表示请求正常处理完毕。
 *      200 OK：客户端请求被正常处理。
 *      206 Partial content：客户端进行了范围请求。
 *  3xx：重定向--要完成请求必须进行更进一步的操作。
 *      301 Moved Permanently：永久重定向，该资源已被永久移动到新位置，
 *          将来任何对该资源的访问都要使用本响应返回的若干个URI之一。
 *      302 Found：临时重定向，请求的资源现在临时从不同的URI中获得。
 *  4xx：客户端错误--请求有语法错误，服务器无法处理请求。
 *      400 Bad Request：请求报文存在语法错误。
 *      403 Forbidden：请求被服务器拒绝。
 *      404 Not Found：请求不存在，服务器上找不到请求的资源。
 *  5xx：服务器端错误--服务器处理请求出错。
 *      500 Internal Server Error：服务器在执行请求时出现错误。
 *      503 Service Unavaliable：服务器正在停机维护。
 * 
 3、有限状态机
 *     有限状态机，是一种抽象的理论模型，它能够把有限个变量描述的状态变化过程，
 * 以可构造可验证的方式呈现出来。比如，封闭的有向图。
 *     有限状态机可以通过if-else,switch-case和函数指针来实现，从软件工程的角
 * 度看，主要是为了封装逻辑。
 *    带有状态转移的有限状态机示例代码。
 *         STATE_MACHINE(){
 *          State cur_State = type_A;
 *          while(cur_State != type_C){
 *              Package _pack = getNewPackage();
 *              switch(){
 *                  case type_A:
 *                      process_pkg_state_A(_pack);
 *                      cur_State = type_B;
 *                      break;
 *                  case type_B:
 *                      process_pkg_state_B(_pack);
 *                      cur_State = type_C;
 *                      break;
 *              }
 *          }
 *      }
 * 该状态机包含三种状态：type_A，type_B和type_C。其中，type_A是初始状态，type_C是结束状态。
 * 
 *****************************************************
 * 
 * 二、本项目http请求部分，细节
 * 
 * 1、http处理流程
 *  (1)浏览器端发出http连接请求，主线程创建<http对象接收请求>并将所有数据读入对应buffer，
 *     将该对象插入任务队列，工作线程从任务队列中取出一个任务进行处理。
 *  (2)工作线程取出任务后，调用process_read函数，通过主、从状态机对请求报文进行解析。
 *  (3)解析完之后，跳转do_request函数生成响应报文，通过process_write写入buffer，返回给浏览器端。
 * 
 * 2、HTTP_CODE表示HTTP请求的处理结果，在头文件中初始化了八种情形，在报文解析时只涉及到四种。
 *  (1)NO_REQUEST：代表请求不完整，需要继续读取请求报文数据
 *  (2)GET_REQUEST: 获得了完整的HTTP请求
 *  (3)BAD_REQUEST: HTTP请求报文有语法错误
 *  (4)INTERNAL_ERROR: 服务器内部错误，该结果在主状态机逻辑switch的default下(一般不会触发)
 * 
 * 3、process_read通过while循环，将主从状态机进行封装，对报文的每一行进行循环处理。
 *  判断条件：
 *      主状态机转移到CHECK_STATE_CONTENT，该条件涉及解析消息体(经历过解析行、解析头之后)
 *      从状态机转移到LINE_OK，该条件涉及解析请求行和请求头部
 *      两者为或关系，当条件为真则继续循环，否则退出
 *  循环体：
 *      从状态机读取数据
 *      调用get_line函数，通过m_start_line将从状态机读取数据 间接赋给text
 *      主状态机解析text
 * 4、从状态机逻辑---->http_conn::LINE_STATUS http_conn::parse_line()
 * (1)判断条件
 *  在HTTP报文中，每一行的数据由\r\n作为结束字符，空行则是仅仅是字符\r\n。因此，
 * 可以通过查找\r\n将报文拆解成单独的行进行解析，项目中便是利用了这一点。
 *  从状态机负责读取buffer中的数据，将每行数据末尾的\r\n置为\0\0，并更新从状态机
 * 在buffer中读取的位置m_checked_idx，以此来驱动主状态机解析。
 * (2)判断步骤：
 *      1.从状态机从m_read_buf中逐字节读取，判断当前字节是否为\r
 *          1.1 接下来的字符是\n，将\r\n修改成\0\0，将m_checked_idx指向下一行的开头，
 *              则返回LINE_OK。算是读完一行；
 *          1.2 接下来达到了buffer末尾，表示buffer还需要继续接收，返回LINE_OPEN；
 *          1.3 否则，表示语法错误，返回LINE_BAD，因为行中间不会出现\r
 *      2.当前字节不是\r，判断是否是\n（一般是上次读取到\r就到了buffer末尾，没有接收
 *          完整，再次接收时会出现这种情况）
 *          2.1如果前一个字符是\r，则将\r\n修改成\0\0，将m_checked_idx指向下一行的开头，
 *              则返回LINE_OK。算是读完一行； 
 *          2.2否则，表示语法错误，返回LINE_BAD，因为行中间不会出现\r
 *      3、当前字节既不是\r，也不是\n，表示接收不完整，需要继续接收，返回LINE_OPEN
 * 
 * 5、主状态机逻辑
 * (1)状态 CHECK_STATE_REQUESTLINE
 *    ---->http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
 *      主状态机初始状态是 CHECK_STATE_REQUESTLINE ，通过调用从状态机来驱动主状态机。
 *  在主状态机进行解析前，从状态机已经将每一行的末尾\r\n符号改为\0\0，以便于主
 *  状态机直接取出对应字符串进行处理。
 *  步骤如下：
 *   a)主状态机的初始状态，调用 parse_request_line 函数解析请求行
 *   b)解析函数从 m_read_buf 中解析 HTTP请求行，获得请求方法、目标URL及HTTP版本号
 *   c)解析完成后主状态机的状态变为 CHECK_STATE_HEADER
 * (2)状态 CHECK_STATE_HEADER
 *    ---->http_conn::HTTP_CODE http_conn::parse_headers(char *text)
 *      解析完请求行后，主状态机继续分析请求头。在报文中，请求头和空行的处理使用
 *  的同一个函数，这里通过判断当前的text首位是不是\0字符，若是，则表示当前处
 *  理的是空行，若不是，则表示当前处理的是请求头。
 *  步骤如下：
 *   a)调用parse_headers函数解析请求头部信息
 *   b)判断是空行还是请求头，若是空行，进而判断content-length是否为0，
 *     如果不是0，表明是POST请求，则状态转移到CHECK_STATE_CONTENT，
 *     否则说明是GET请求，则报文解析结束。
 *   c)若解析的是请求头部字段，则主要分析connection字段，content-length字段，
 *     其他字段可以直接跳过(其实这里，因为用不到所以没分析，没写那么细节)，各位也可以根据需求继续分析。
 *      c1)connection字段判断是keep-alive还是close，决定是长连接还是短连接
 *      c2)content-length字段，这里用于读取post请求的消息体长度,get的话 不用管
 * (3)状态 CHECK_STATE_CONTENT
 *    ---->http_conn::HTTP_CODE http_conn::parse_content(char *text)
 *     a)仅用于解析POST请求，调用parse_content函数解析消息体
 *     b)用于保存post请求消息体，为后面的登录和注册做准备
 * 
 *****************************************************
 * 
 * 三、本项目http相应部分，细节
 * 
 * 1、基础API部分，介绍stat、mmap、iovec、writev。
 *  (1)stat函数：用于取得指定文件的文件属性，并将文件属性存储在结构体stat里，
 *  (2)mmap函数：用于将一个文件或其他对象映射到内存，提高文件的访问速度。
 *  (3)iovec函数：定义了一个向量元素，通常，这个结构用作一个多元素的数组。
 *  (4)writev函数：writev函数用于在一次函数调用中写多个非连续缓冲区，
 *     有时也将这该函数称为聚集写。对应的就是分散读了 readv函数，嘿嘿
 * 
 * 2、流程图部分，描述服务器端响应请求报文的逻辑，各模块间的关系。
 *  m_url为请求报文中解析出的请求资源，以/开头，也就是/xxx，项目中解析后的m_url有5种情况。
 *  (1) /   GET请求，跳转到judge.html，即欢迎访问界面; action属性设置为0和1
 *  (2) /0  GET请求，跳转到register.html，即注册界面; action属性设置为3check.cgi
 *  (3) /1  GET请求，跳转到log.html，即登录界面; action属性设置为2check.cgi
 *  (4) /2check.cgi POST请求，进行登录校验;
 *          验证成功跳转到welcome.html，即资源请求成功界面;
 *          验证失败跳转到logError.html，即登录失败界面;
 *  (5) /3check.cgi POST请求，进行注册校验;
 *          注册成功跳转到log.html，即登录界面;
 *          注册失败跳转到registerError.html，即注册失败界面
 *  (6) /test.jpg   GET请求，请求服务器上的图片资源
 * 
 *****************************************************
 * 
 * 四、epoll函数
 *  epoll设计的知识比较多，这里仅对API和基础知识介绍。
 *  epoll使用一组函数完成任务，其将用户关心的文件描述符上的事件放进内核中的一个
 *  事件表中，避免反复在内核态和用户态之间复制整个文件描述符集，但是epoll需要一
 *  个额外的文件描述符，来唯一标识内核中的事件表，也就是epfd--epoll_create的
 *  返回值
 * 
 *  1、epoll_create 函数
 *      创建一个指示epoll内核事件表的文件描述符，
 *      该描述符将用作其他epoll系统调用的第一个参数，size不起作用。
 *  函数定义：
 *  #include <sys/epoll.h>
 *  int epoll_create(int size)
 * 
 *  2、epoll_ctl函数
 *  该函数用于操作内核事件表监控的文件描述符上的事件：注册、修改、删除
 *  函数定义：
 *  #include <sys/epoll.h>
 *  int epoll_ctl(int epfd, //为epoll_creat的句柄 就是epoll_create函数的返回值
 *                int op, //表示动作，用3个宏来表示：
 *                        //EPOLL_CTL_ADD (注册新的fd到epfd)，
 *                        //EPOLL_CTL_MOD (修改已经注册的fd的监听事件)，
 *                        //EPOLL_CTL_DEL (从epfd删除一个fd)；
 *                int fd, //要操作的文件描述符
 *                struct epoll_event *event)//告诉内核需要监听的事件
 * 上述event是epoll_event结构体指针类型，表示内核所监听的事件，具体定义如下：
 *      struct epoll_event {
 *          __uint32_t events;  //epoll事件 events
 *          3epoll_data_t data; //用户数据
 *          };
 * 其中：events描述事件类型，其中epoll事件类型有以下几种：
 *      (1)EPOLLIN：表示对应的文件描述符可以读（包括对端SOCKET正常关闭）
 *      (2)EPOLLOUT：表示对应的文件描述符可以写
 *      (3)EPOLLPRI：表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）
 *      (4)EPOLLERR：表示对应的文件描述符发生错误
 *      (5)EPOLLHUP：表示对应的文件描述符被挂断；
 *      (6)EPOLLET：将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)而言的
 *      (7)EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里
 * 
 * 3、epoll_wait函数
 *  #include <sys/epoll.h>
 *  int epoll_wait(int epfd, 
 *                 struct epoll_event *events, //用来存内核得到事件的集合，
 *                 int maxevents, //告之内核这个events有多大，这个maxevents的值不能大于创建epoll_create()时的size，
 *                 int timeout)//是超时时间：-1：阻塞；0：立即返回，非阻塞；>0：指定毫秒
 *  该函数用于等待所监控文件描述符上有事件的产生，返回就绪的文件描述符个数，时间到时返回0，出错返回-1
 * 
 * 4、项目中epoll相关代码部分包括
 *      (1)非阻塞模式：int setnonblocking(int fd)
 *      (2)内核事件表注册事件：void addfd(int epollfd, int fd, bool one_shot)
 *          开启EPOLLONESHOT，针对客户端连接的描述符，listenfd不用开启
 *      (3)内核事件表删除事件：void removefd(int epollfd, int fd)
 *      (4)重置EPOLLONESHOT事件:void modfd(int epollfd, int fd, int ev)
 *     
 * 
 * 
 *****************************************************
 * 
*/




#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include<mysql/mysql.h>

//定义http响应的一些状态信息
const char* ok_200_title="OK";
const char* error_400_title="Bad Request";
const char* error_400_form="Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title="Forbidden";
const char* error_403_form="You do not have permission to get file form this server.\n";
const char* error_404_title="Not Found";
const char* error_404_form="The requested file was not found on this server.\n";
const char* error_500_title="Internal Error";
const char* error_500_form="There was an unusual problem serving the request file.\n";

//网站根目录，文件夹内存放请求的资源和跳转的html文件
//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char* doc_root="/home/qgy/github/ini_tinywebserver/root";

//创建数据库连接池
connection_pool *connPool=connection_pool::GetInstance("localhost","root","root","qgydb",3306,5);

//将表中的用户名和密码放入map
map<string,string> users;



//初始化数据库读取表
void http_conn::initmysql_result(){
    //先从连接池中取一个连接
    MYSQL *mysql=connPool->GetConnection();

    //在user表中检索username，passwd数据，浏览器端输入
    if(mysql_query(mysql,"SELECT username,passwd FROM user"))
    {
        //printf("INSERT error:%s\n",mysql_error(mysql));
        LOG_ERROR("SELECT error:%s\n",mysql_error(mysql));
            //return BAD_REQUEST;
    }

    //从表中检索完整的结果集
    MYSQL_RES *result=mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields=mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields=mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row=mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1]=temp2;
    }
    //将连接归还连接池
    connPool->ReleaseConnection(mysql);

}


//对文件描述符设置非阻塞
int setnonblocking(int fd){
    //F_GETFL：获取文件打开方式的标志，标志值含义与open调用一致
    //下面的操作就是增加文件的某个flags，比如文件是阻塞的，想设置成非阻塞:
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

//将内核事件表注册读事件，如果是ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd,int fd,bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;
    if(one_shot)
        event.events|=EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd,int fd)
{
        epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
        close(fd);
}

//将事件设置为ev  EPOLLIN或者EPOLLOUT
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int http_conn::m_user_count=0;
int http_conn::m_epollfd=-1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if(real_close&&(m_sockfd!=-1))
    {
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd,const sockaddr_in& addr)
{
    m_sockfd=sockfd;
    m_address=addr;
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd,sockfd,true);
    m_user_count++;
    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    m_check_state=CHECK_STATE_REQUESTLINE;
    m_linger=false;
    m_method=GET;
    m_url=0;
    m_version=0;
    m_content_length=0;
    m_host=0;
    m_start_line=0;
    m_checked_idx=0;
    m_read_idx=0;
    m_write_idx=0;
    cgi = 0;
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);
}

//从状态机的判断函数，用于分析出一行内容，根据不同情况，会有三种状态
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
//m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
//m_checked_idx指向从状态机当前正在分析的字节
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    //一行一行读取嘛
    for(;m_checked_idx<m_read_idx;++m_checked_idx)
    {
        ////temp代表将要分析的字节 一个字节一个字节来
        temp=m_read_buf[m_checked_idx];
        //1、如果当前是\r字符，则有可能会读取到完整行
        if(temp=='\r'){
            //下一个字符达到了buffer结尾，则接收不完整，需要继续接收
            if((m_checked_idx+1)==m_read_idx)
                return LINE_OPEN;
            //下一个字符是\n，将\r\n改为\0\0 此时读取到的是完整一行，并且做出了调整
            else if(m_read_buf[m_checked_idx+1]=='\n'){
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            //如果都不符合，则返回语法错误，因为行中间不可能出现\r
            return LINE_BAD;
        }
        //2、如果当前字符是\n，也有可能读取到完整行-\>那就是上次读取到\r 没了 这次继续上了
        //一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if(temp=='\n')
        {
            //前一个字符是\r，则接收完整
            if(m_checked_idx>1&&m_read_buf[m_checked_idx-1]=='\r')
            {
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            //如果都不符合，则返回语法错误，因为行中间不可能出现\r
            return LINE_BAD;
        }
    }
    //并没有找到\r\n，需要继续接收
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//读取到m_read_buffer中，并更新m_read_idx。
//非阻塞ET工作模式下，需要一次性将数据读完(因为ET模式下，只会出发一次)
bool http_conn::read_once()
{
    //m_read_idx ： 缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    //READ_BUFFER_SIZE ： 读取缓冲区m_read_buf大小
    //m_read_idx>=READ_BUFFER_SIZE 代表越界了
    if(m_read_idx>=READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read=0;
    //while 直到return
    //循环读取客户数据，直到无数据可读或对方关闭连接
    while(true)
    {
        //接受m_sockfd 传来的数据
        //从m_read_buf+m_read_idx位置
        //读取READ_BUFFER_SIZE-m_read_idx 这么多数据
        bytes_read=recv(m_sockfd,           //第一个参数指定接收端套接字描述符
                        m_read_buf+m_read_idx,//第二个参数指明一个缓冲区，该缓冲区用来存放recv函数接收到的数据；
                        READ_BUFFER_SIZE-m_read_idx,//第三个参数指明buf的长度；
                        0);     //第四个参数一般置0。
        //如果读取失败
        if(bytes_read==-1)
        {
            //读取到了
            if(errno==EAGAIN||errno==EWOULDBLOCK)
                break;
            //否则就是失败
            return false;
        }
        else if(bytes_read==0)
        {
            //读空了
            return false;
        }
        //读取到了，指针后移
        m_read_idx+=bytes_read;
    }
    return true;
}


//解析头或者解析行函数触发的前提是  从状态机 LINE_OK了
//此时主状态机的状态是初始状态 CHECK_STATE_REQUESTLINE
//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //在HTTP报文中，请求行是用来说明<请求类型>,<要访问的资源>，<所使用的HTTP版本>
    //其中各个部分之间通过<\t>或<空格>分隔。
    //请求行中最先含有<空格>和<\t>任一字符的位置并返回
    //strpbrk函数，用来比较字符串str1和str2中是否有相同的字符，返回该字符在str1中的位置的指针。
    m_url=strpbrk(text," \t");
    //如果没有空格或\t，则报文格式有误
    if(!m_url)
    {
        return BAD_REQUEST;
    }
    //如果有空格或者\t，那就将该位置改为\0，用于将前面数据取出
    *m_url++='\0';

    //取出数据，并通过与GET和POST比较，以确定请求方式
    char *method=text;
    if(strcasecmp(method,"GET")==0)
        m_method=GET;
    else if(strcasecmp(method,"POST")==0)
    {
        m_method=POST;
        cgi=1;
    }
    else
        return BAD_REQUEST;

    //m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    //将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    //strspn函数，检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_url+=strspn(m_url," \t");
    //然后找\t
    //使用与判断请求方式的相同逻辑，判断HTTP版本号
    m_version=strpbrk(m_url," \t");
    if(!m_version)
        return BAD_REQUEST;
    *m_version++='\0';
    m_version+=strspn(m_version," \t");
    //仅支持HTTP/1.1
    if(strcasecmp(m_version,"HTTP/1.1")!=0)
        return BAD_REQUEST;
    //对请求资源前7个字符进行判断
    //这里主要是有些报文的请求资源中会带有http://，这里需要对这种情况进行单独处理
    if(strncasecmp(m_url,"http://",7)==0)
    {
        m_url+=7;
        m_url=strchr(m_url,'/');
    }

	//同样增加https情况
    if(strncasecmp(m_url,"https://",8)==0)
    {
        m_url+=8;
        m_url=strchr(m_url,'/');
    }

    //一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if(!m_url||m_url[0]!='/')
        return BAD_REQUEST;
    
    //当url为/时，显示判断界面
    //TODO：这里更改主页面，就是注册页面
    if(strlen(m_url)==1)
        strcat(m_url,"judge.html");
    //请求行处理完毕，将主状态机转移处理请求头
    m_check_state=CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析头或者解析行函数触发的前提是  从状态机 LINE_OK了
//此时主状态机处于CHECK_STATE_HEADER状态
//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    //判断是空行还是请求头
    //如果是空行 那就跳过，转入CHECK_STATE_CONTENT状态
    if(text[0]=='\0')
    {
        if(m_content_length!=0)
        {
            //POST需要跳转到消息体处理状态
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    //解析请求头部连接字段
    else if(strncasecmp(text,"Connection:",11)==0)
    {
        text+=11;
        //跳过空格和\t字符
        text+=strspn(text," \t");
        //解析keep-alive
        if(strcasecmp(text,"keep-alive")==0)
        {
            //如果是长连接，则设置标志位，其实就是延时定时时间长点
            m_linger=true;
        }
    }
    //解析请求头部内容长度字段
    else if(strncasecmp(text,"Content-length:",15)==0)
    {
        text+=15;
        text+=strspn(text," \t");
        m_content_length=atol(text);
    }
    //解析请求头部HOST字段，按照需要来吧
    else if(strncasecmp(text,"Host:",5)==0)
    {
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }
    else{
        //printf("oop!unknow header: %s\n",text);
	LOG_INFO("oop!unknow header: %s",text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}


//只有在post方法中会被调用，此时为了保存数据嘛
//判断http请求是否被完整读入
//服务器端解析浏览器的请求报文，
//当解析为POST请求时，cgi标志位设置为1，并将请求报文的消息体赋值给 m_string
//进而提取出用户名和密码。
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    //判断buffer中是否读取了消息体
    if(m_read_idx>=(m_content_length+m_checked_idx)){
        text[m_content_length]='\0';
	//POST请求中最后为输入的用户名和密码
    //TODO：可以放别的，改html文件，post别的数据就ok
	    m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}



/**
 * @name: http_conn::process_read
 * @desc:处理http读动作
 * @return:返回两部分，一个是语法错误导致得BAD_REQUEST；还有是do_request函数结果
 */
http_conn::HTTP_CODE http_conn::process_read()
{
    //初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;
    char* text=0;
    //printf("=========请求行头=========\n");
    //LOG_INFO("=========请求行头=========\n");
    //Log::get_instance()->flush();


    /**
     * 这里判断条件写成这样其实是为了为了避免将用户名和密码直接暴露在URL中，
     * 我们在项目中改用了POST请求，将用户名和密码添加在报文中作为消息体
     * 进行了封装。
     * 
     * 1、line_status=parse_line())==LINE_OK
     *     在GET请求报文中，每一行都是\r\n作为结束，所以对报文进行拆解时，
     * 仅用从状态机的状态line_status=parse_line())==LINE_OK语句即可。
     * 2、m_check_state==CHECK_STATE_CONTENT
     *     但是，在POST请求报文中，消息体的末尾没有任何字符，所以不能使用从状态机的状态，
     * 这里转而使用主状态机的状态作为循环入口条件。
     * 3、&& line_status==LINE_OK
     *     解析完消息体后，报文的完整解析就完成了，但此时主状态机的状态还是
     * CHECK_STATE_CONTENT，、也就是说，符合循环入口条件，还会再次进入循环，
     * 这并不是我们所希望的。增加了该语句，并在完成消息体解析后，将line_status
     * 变量更改为LINE_OPEN，此时可以跳出循环，完成报文解析任务。
    */
    //条件1：主状态机转移到CHECK_STATE_CONTENT，该条件涉及解析消息体(经历过解析行、解析头之后)
    //条件2：从状态机转移到LINE_OK，该条件涉及解析请求行和请求头部
    //parse_line为从状态机的具体实现
    while((m_check_state==CHECK_STATE_CONTENT&&line_status==LINE_OK)||((line_status=parse_line())==LINE_OK))
    {
        //text 为整行数据 不包含换行和回车
        text=get_line();

        //m_start_line 是每一个数据行在m_read_buf中的起始位置
        //m_checked_idx 表示从状态机在m_read_buf中读取的位置
        m_start_line=m_checked_idx;
        //printf("======:%s\n",text);
	    LOG_INFO("%s",text);
    	Log::get_instance()->flush();

        ////主状态机的状态，利用switch实现状态机
        switch(m_check_state)
        {
            //检测状态请求行
            case CHECK_STATE_REQUESTLINE:
            {
                //解析http请求行，获得请求方法，目标url及http版本号
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            //检测请求头
            case CHECK_STATE_HEADER:
            {
                ret=parse_headers(text);
                if(ret==BAD_REQUEST)
                    return BAD_REQUEST;
                //完整解析GET请求后，跳转到报文响应函数 do_request()
                else if(ret==GET_REQUEST)//get方法才能，post得解析内容
                {
                    return do_request();
                }
                break;
            }
            //检测请求数据
            case CHECK_STATE_CONTENT:
            {
                ret=parse_content(text);
                //完整解析POST请求后，跳转到报文响应函数
                if(ret==GET_REQUEST)
                    return do_request();
                //解析完消息体即完成报文解析，避免再次进入循环，更新line_status
                line_status=LINE_OPEN;
                break;
            }
            default:
            return INTERNAL_ERROR;
        }
    }
    //printf("=========请求行end=========\n");
    return NO_REQUEST;
}


//该函数将网站根目录和url文件拼接，然后通过stat判断该文件属性。
//另外，为了提高访问速度，通过mmap进行映射，将普通文件映射到内存逻辑地址。
http_conn::HTTP_CODE http_conn::do_request()
{
    //将初始化的m_real_file赋值为网站根目录
    strcpy(m_real_file,doc_root);
    //接下来要在doc_root查文件了
    int len=strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    //找到m_url中/的位置
    const char *p = strrchr(m_url, '/'); 

//#if 0
    //处理cgi
    //实现登录和注册校验
    if(cgi==1 && (*(p+1) == '2' || *(p+1) == '3'))
    {
	
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];
        //printf("====+++====+++%c\n", flag);

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url+2);
            strncpy(m_real_file+len,m_url_real,FILENAME_LEN-len-1);
        free(m_url_real);
        
        //将用户名和密码提取出来
        //user=123&passwd=123
            char name[100],password[100];
            int i;
            //以 & 字符为分割符，前面账号，后面密码
            for(i=5;m_string[i]!='&';++i)
                name[i-5]=m_string[i];
            name[i-5]='\0';

            int j=0;
            for(i=i+10;m_string[i]!='\0';++i,++j)
                password[j]=m_string[i];
            password[j]='\0';

//同步线程登录校验
//#if 0
        pthread_mutex_t lock;
            pthread_mutex_init(&lock, NULL);

        //从连接池中取一个连接
        MYSQL *mysql=connPool->GetConnection();
        
        //如果是注册，先检测数据库中是否有重名的
        //没有重名的，进行增加数据
        char *sql_insert = (char*)malloc(sizeof(char)*200);
        strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
        strcat(sql_insert, "'");
        strcat(sql_insert, name);
        strcat(sql_insert, "', '");
        strcat(sql_insert, password);
        strcat(sql_insert, "')");
        
        if(*(p+1) == '3'){
            if(users.find(name)==users.end()){

                pthread_mutex_lock(&lock);
                int res = mysql_query(mysql,sql_insert);
                users.insert(pair<string, string>(name, password));
                pthread_mutex_unlock(&lock);

                if(!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if(*(p+1) == '2'){
            if(users.find(name)!=users.end()&&users[name]==password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
        connPool->ReleaseConnection(mysql);
//#endif

//CGI多进程登录校验
#if 0
	//fd[0]:读管道，fd[1]:写管道
        pid_t pid;
        int pipefd[2];
        if(pipe(pipefd)<0)
        {
            LOG_ERROR("pipe() error:%d",4);
            return BAD_REQUEST;
        }
        if((pid=fork())<0)
        {
            LOG_ERROR("fork() error:%d",3);
            return BAD_REQUEST;
        }
	
        if(pid==0)
        {
	    //标准输出，文件描述符是1，然后将输出重定向到管道写端
            dup2(pipefd[1],1);
	    //关闭管道的读端
            close(pipefd[0]);
	    //父进程去执行cgi程序，m_real_file,name,password为输入
	    //./check.cgi name password

            execl(m_real_file,&flag,name,password, NULL);
        }
        else{
	    //printf("子进程\n");
	    //子进程关闭写端，打开读端，读取父进程的输出
            close(pipefd[1]);
            char result;
            int ret=read(pipefd[0],&result,1);

            if(ret!=1)
            {
                LOG_ERROR("管道read error:ret=%d",ret);
                return BAD_REQUEST;
            }
	    if(flag == '2'){
		    //printf("登录检测\n");
		    LOG_INFO("%s","登录检测");
    		    Log::get_instance()->flush();
		    //当用户名和密码正确，则显示welcome界面，否则显示错误界面
		    if(result=='1')
			strcpy(m_url, "/welcome.html");
		        //m_url="/welcome.html";
		    else
			strcpy(m_url, "/logError.html");
		        //m_url="/logError.html";
	    }
	    else if(flag == '3'){
		    //printf("注册检测\n");
		    LOG_INFO("%s","注册检测");
    		    Log::get_instance()->flush();
		    //当成功注册后，则显示登陆界面，否则显示错误界面
		    if(result=='1')
			strcpy(m_url, "/log.html");
			//m_url="/log.html";
		    else
			strcpy(m_url, "/registerError.html");
			//m_url="/registerError.html";
	    }
	    //printf("m_url:%s\n", m_url);
	    //回收进程资源
            waitpid(pid,NULL,0);
	    //waitpid(pid,0,NULL);
	    //printf("回收完成\n");
        }
#endif
    }


    //如果请求资源为/0，表示跳转注册界面
    if(*(p+1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);

        //将网站目录和/register.html进行拼接
        //TODO：可以修改
        strcpy(m_url_real,"/register.html");
        //更新到 m_real_file 中
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        
        free(m_url_real);
    }
    //如果请求资源为/1，表示跳转登录界面
    else if( *(p+1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real,"/log.html");
        ////将网站目录和/log.html进行拼接，更新到m_real_file中
        //TODO：可以修改
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        
        free(m_url_real);
    }
    else
        //如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
        //这里的情况是welcome界面，请求服务器上的一个图片
        //TODO：可以修改
    	strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);


    //需要三个条件：1、文件存在；2、文件有权限；3、文件不是目录
    //通过 stat 获取请求资源文件信息，成功则将信息更新到 m_file_stat 结构体
    //失败返回 NO_RESOURCE 状态，表示资源不存在
    //TODO：可以修改
    if(stat(m_real_file,&m_file_stat)<0)
        return NO_RESOURCE;

    //判断文件的权限，是否可读，不可读则返回 FORBIDDEN_REQUEST 状态
    //TODO：可以修改
    if(!(m_file_stat.st_mode&S_IROTH))
        return FORBIDDEN_REQUEST;
        
    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    //TODO：可以修改
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd=open(m_real_file,O_RDONLY);
    //如果文件存在  利用mmap映射到内存当中，等待发送给客户端
    m_file_address=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    //避免文件描述符的浪费和占用
    close(fd);
    //否则，则表示请求文件存在，且可以访问
    return FILE_REQUEST;
}


void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}

/**http_conn::write()流程
 * 首先初始化剩余发送数据和已发送数据大小，byte_to_send和byte_have_send，
 * 然后通过writev函数循环发送响应报文数据，并判断响应报文整体是否发送成功。
 * 
 * (1)若writev单次发送成功，更新byte_to_send和byte_have_send的大小，
 *    若响应报文整体发送成功,则取消mmap映射,并判断是否是长连接.
 *      (a)长连接重置http类实例，注册读事件，不关闭连接，
 *      (b)短连接直接关闭连接
 * (2)若writev单次发送不成功，判断是否是写缓冲区满了。
 *      (a)若不是因为缓冲区满了而失败，取消mmap映射，关闭连接
 *      (b)若eagain则满了，注册写事件，等待下一次写事件触发（当写缓冲区从不可写变为可写，触发epollout），因此在此期间无法立即接收到同一用户的下一请求，但可以保证连接的完整性。
*/
bool http_conn::write()
{
    int temp=0;
    int bytes_have_send=0;

    //这里不科学 
    //将要发送的数据长度初始化为响应报文缓冲区长度
    //bytes_to_send 将要发送的数据长度
    //m_write_idx 相应报文缓冲区长度
    int bytes_to_send = m_write_idx;

    //若要发送的数据长度为0
    //表示响应报文为空，一般不会出现这种情况
    if(bytes_to_send == 0)
    {
        //将事件重置为EPOLLONESHOT
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        //重新初始化HTTP对象
        init();
        return true;
    }

    //这里的循环非常鸡肋，下面会详述
    while(1)
    {
        //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp=writev(m_sockfd,m_iv,m_iv_count);
        //printf("temp:%d\n",temp);
        //成功发送，则返回temp字节数，发送失败需要判断是缓冲区满了，还是发送错误
        if(temp<=-1)
        {
            //判断缓冲区是否满了
            if(errno==EAGAIN)
            {
                //重新注册写事件
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            //如果发送失败，但不是缓冲区问题，取消映射
            unmap();
            return false;
        }
        //更新剩余数据和已发送数据
        bytes_to_send -= temp;
        bytes_have_send += temp;

        //你是不是很疑惑这里？？下面会详细解释这里      
        //剩余数据和已发送数据比较，判断是否发送完。
        if(bytes_to_send<=0)
        {
            unmap();
            //浏览器的请求为长连接
            if(m_linger)
            {
                //printf("========================\n");
                //printf("%s\n", "发送响应成功");
                //重新初始化HTTP对象
                init();
                //在epoll树上重新注册读事件
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else
            {
                //若为短连接，主线程中从epoll树上删除该描述符
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return false;
            }
        }
    }
} 

/**
 * @name: add_response
 * @desc: 向后一个变量中添加响应内容,类似一个模板函数
 * @param {const char*} format
 * @return {*}
 */
bool http_conn::add_response(const char* format,...)
{
    //如果写入内容超出m_write_buf大小则报错
    if(m_write_idx>=WRITE_BUFFER_SIZE)
        return false;

    //定义可变参数列表
    va_list arg_list;

    //将变量arg_list初始化为传入参数
    va_start(arg_list,format);

    //将数据 format 从可变参数列表写入缓冲区写，返回写入数据的长度
    int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx)){
        va_end(arg_list);
        return false;
    }
    //否则，更新 m_write_idx 位置
    m_write_idx+=len;
    //清空可变参列表
    va_end(arg_list);
    //printf("%s\n",m_write_buf);
    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
    return true;
}

//添加状态行
bool http_conn::add_status_line(int status,const char* title)
{
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

//添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len)
{
    //add_content_type();
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
//添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n",content_len);
}
//添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n","text/html");
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n",(m_linger==true)?"keep-alive":"close");
}
//添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s","\r\n");
}
//添加文本content
bool http_conn::add_content(const char* content)
{
    return add_response("%s",content);
}

/**
 * @name: http_conn::process_write
 * @desc:根据do_request的返回状态，
 *       服务器子线程调用 process_write 向 m_write_buf 中写入响应报文。
 * @param {HTTP_CODE} ret
 * @return {*}
 */
bool http_conn::process_write(HTTP_CODE ret)
{
    /**
     * (1)add_status_line函数，添加状态行：http/1.1 状态码 状态消息
     * (2)add_headers函数,添加消息报头：内部调用add_content_length和add_linger函数
     *      content-length 记录响应报文长度，用于浏览器端判断服务器是否发送完数据
     *      connection 记录连接状态，用于告诉浏览器端保持长连接
     * (3)add_blank_line函数，添加空行
    */

    /**
     * 响应报文分为两种，
     * (1)一种是请求文件的存在，通过io向量机制iovec，声明两个iovec，
     *      第一个指向 m_write_buf，
     *      第二个指向 mmap 的地址 m_file_address ；
     * (2)一种是请求出错，这时候只申请一个 iovec ，指向 m_write_buf 。
     *      其中iovec是一个结构体，里面有两个元素，
     *      指针成员iov_base指向一个缓冲区，这个缓冲区是存放的是writev将要发送的数据。
     *      成员iov_len表示实际写入的长度
    */
    switch(ret)
    {
        //内部错误，500
        case INTERNAL_ERROR:
            {
                //状态行
                add_status_line(500,error_500_title);
                //消息报头
                add_headers(strlen(error_500_form));
                if(!add_content(error_500_form))
                    return false;
            break;
            }
        //报文语法有误，404
        case BAD_REQUEST:
            {
                add_status_line(404,error_404_title);
                add_headers(strlen(error_404_form));
                if(!add_content(error_404_form))
                    return false;
                break;
            }
        //资源没有访问权限，403
        case FORBIDDEN_REQUEST:
            {
                add_status_line(403,error_403_title);
                add_headers(strlen(error_403_form));
                if(!add_content(error_403_form))
                    return false;
                break;
            }
        //文件存在，200 成功！！！
        case FILE_REQUEST:
            {
                add_status_line(200,ok_200_title);
                //如果请求的资源存在
                if(m_file_stat.st_size!=0)
                {
                    add_headers(m_file_stat.st_size);
                    //第一次发送，发送两部分，首先是iovec[0]发送响应报文缓冲区，
                    //第二次发送，m_iv[1]发送被映射到内存的文件，返回对应文件的指针以及文件的大小 方便用来发送嘛
                    //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx-->状态行
                    m_iv[0].iov_base=m_write_buf;
                    m_iv[0].iov_len=m_write_idx;

                    //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                    m_iv[1].iov_base=m_file_address;
                    m_iv[1].iov_len=m_file_stat.st_size;
                    m_iv_count=2;
                    return true;
                }
                else
                {
                    //如果请求的资源大小为0，则返回空白html文件
                    const char* ok_string="<html><body></body></html>";
                    add_headers(strlen(ok_string));
                    if(!add_content(ok_string))
                        return false;
                }
            }
        default:
            return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base=m_write_buf;
    m_iv[0].iov_len=m_write_idx;
    m_iv_count=1;
    return true;
}


//HTTP的工作总函数，先读取，后响应
void http_conn::process()
{
    //先处理读，也就是接受HTTP请求报文
    HTTP_CODE read_ret=process_read();

    //NO_REQUEST 代表请求不完整，需要继续接受请求数据
    if(read_ret==NO_REQUEST)
    {
        //注册并监听读事件
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }


    //处理写，也就是发送HTTP响应报文
    //请求完整了，调用process_write完成报文响应
    bool write_ret=process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    //注册 epollout 事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
