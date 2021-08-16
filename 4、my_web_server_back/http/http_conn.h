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
 * 3、有限状态机
 *     有限状态机，是一种抽象的理论模型，它能够把有限个变量描述的状态变化过程，
 * 以可构造可验证的方式呈现出来。比如，封闭的有向图。
 *     有限状态机可以通过if-else,switch-case和函数指针来实现，从软件工程的角
 * 度看，主要是为了封装逻辑。
 *     带有状态转移的有限状态机示例代码。
 *         STATE_MACHINE(){
            State cur_State = type_A;
            while(cur_State != type_C){
                Package _pack = getNewPackage();
                switch(){
                    case type_A:
                        process_pkg_state_A(_pack);
                        cur_State = type_B;
                        break;
                    case type_B:
                        process_pkg_state_B(_pack);
                        cur_State = type_C;
                        break;
                }
            }
        }
 * 该状态机包含三种状态：type_A，type_B和type_C。其中，type_A是初始状态，type_C是结束状态。
 * 
 * 本项目中，从状态机负责读取报文的一行，主状态机负责对该行进行数据分析，主状态机内部调用从状态机，
 * 从状态机驱动主状态机
 * 
 * (1)主状态机
 *  三种状态，标识当前解析位置
 *      a)CHECK_STATE_REQUESTLINE，解析请求行
 *      b)CHECK_STATE_HEADER，解析请求头
 *      c)CHECK_STATE_CONTENT，解析消息体，仅用于解析POST请求
 * (2)从状态机
 *  三种状态，标识解析一行的读取状态。
 *      a)LINE_OK，完整读取一行
 *      b)LINE_BAD，报文语法有误
 *      c)LINE_OPEN，读取的行不完整
 * 
 * 
 * 4、http处理流程
 *  (1)浏览器端发出http连接请求，主线程创建<http对象接收请求>并将所有数据读入对应buffer，
 *     将该对象插入任务队列，工作线程从任务队列中取出一个任务进行处理。
 *  (2)工作线程取出任务后，调用process_read函数，通过主、从状态机对请求报文进行解析。
 *  (3)解析完之后，跳转do_request函数生成响应报文，通过process_write写入buffer，返回给浏览器端。
*/



/**
 * 1、浏览器端发出http连接请求，服务器端主线程创建http对象接收请求并将
 * 所有数据读入对应buffer，将该对象插入任务队列后，工作线程从任务队列
 * 中取出一个任务进行处理。
 * 
 * 
*/

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/wait.h>
#include<sys/uio.h>
#include"../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"


class http_conn{
    public:
        //设置读取文件的名称m_real_file大小
        static const int FILENAME_LEN=200;
        //设置读取缓冲区m_read_buf大小
        static const int READ_BUFFER_SIZE=2048;
        //设置写缓冲区m_write_buf大小
        static const int WRITE_BUFFER_SIZE=1024;
        //报文请求方法，常用的有GET,POST,PUT,DELETE，这只用到了get和post
        enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATH};
        //主状态机的三个状态：
        enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,//检测状态请求行
                         CHECK_STATE_HEADER,        //检测请求头
                         CHECK_STATE_CONTENT};      //检测请求数据
        //报文解析的结果
        enum HTTP_CODE{NO_REQUEST,//请求不完整，需要继续读取请求报文数据；跳转主线程继续监测读事件
                       GET_REQUEST,//获得了完整的HTTP请求；调用do_request完成请求资源映射
                       BAD_REQUEST,//HTTP请求报文有语法错误或请求资源为目录；跳转process_write完成响应报文
                       NO_RESOURCE,//请求资源不存在；跳转process_write完成响应报文
                       FORBIDDEN_REQUEST,//请求资源禁止访问，没有读取权限；跳转process_write完成响应报文
                       FILE_REQUEST,//请求资源可以正常访问；跳转process_write完成响应报文
                       INTERNAL_ERROR,//服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
                       CLOSED_CONNECTION};//关闭连接
        //从状态机状态，一行一行读取
        enum LINE_STATUS{LINE_OK=0,     //行ok
                         LINE_BAD,      //行错误
                         LINE_OPEN};    //行在读
    public:
        //构造函数
        http_conn(){}
        //析构函数
        ~http_conn(){}
    public:
        //初始化套接字地址，函数内部会调用私有方法init
        void init(int sockfd,const sockaddr_in &addr);
        //关闭http连接
        void close_conn(bool real_close=true);
        //处理数据函数
        void process();
        //读取浏览器端发来的所有数据
        bool read_once();
        //响应报文的写入函数
        bool write();

        //获取地址
	sockaddr_in *get_address(){
		return &m_address;	
	}
        //初始化数据库读取表
	void initmysql_result();
    
    private:
        void init();
        //从m_read_buf读取，并处理请求报文，返回解析状态
        HTTP_CODE process_read();

        //向m_write_buf写入响应报文数据
        bool process_write(HTTP_CODE ret);

        //主状态机解析报文中的请求行数据
        HTTP_CODE parse_request_line(char *text);
        //主状态机解析报文中的请求头数据
        HTTP_CODE parse_headers(char *text);
        //主状态机解析报文中的请求内容的数据
        HTTP_CODE parse_content(char *text);
        
        //生成响应报文
        HTTP_CODE do_request();

        //m_start_line 是已经解析的字符个数
        //get_line用于将指针向后偏移，指向未处理的字符
        //m_read_buf+m_start_line  读缓冲区地址加偏移量

        //m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text
        //此时从状态机已提前将一行的末尾字符\r\n变为\0\0，
        //所以text可以直接取出完整的行进行解析
        char* get_line(){return m_read_buf+m_start_line;};

        //从状态机读取一行，分析是请求报文的哪一部分，返回从状态机的状态
        LINE_STATUS parse_line();


        //
        void unmap();
        
        //根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
        bool add_response(const char* format,...);
        bool add_content(const char* content);
        bool add_status_line(int status,const char* title);
        bool add_headers(int content_length);
	    bool add_content_type();
        bool add_content_length(int content_length);
        bool add_linger();
        bool add_blank_line();


    public:
        //epoll文件描述符
        static int m_epollfd;
        //连接的用户的数量
        static int m_user_count;


    private:

        int m_sockfd;
        sockaddr_in m_address;

        //存储读取的请求报文数据
        char m_read_buf[READ_BUFFER_SIZE];

        //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
        int m_read_idx;

        //m_read_buf读取的位置m_checked_idx
        int m_checked_idx;
        //m_read_buf中已经解析的字符个数
        int m_start_line;

        //存储发出的响应报文数据
        char m_write_buf[WRITE_BUFFER_SIZE];
        //指示buffer中的长度
        int m_write_idx;
        
        ////主状态机的状态
        CHECK_STATE m_check_state;

        //请求方法
        METHOD m_method;

        //以下为解析请求报文中对应的6个变量
        /**
         * HOST，给出请求资源所在服务器的域名。
         * User-Agent，HTTP客户端程序的信息，该信息由你发出请求使用的浏览器来定义,并且在每个请求中自动发送等。
         * Accept，说明用户代理可处理的媒体类型。
         * Accept-Encoding，说明用户代理支持的内容编码。
         * Accept-Language，说明用户代理能够处理的自然语言集。
         * Content-Type，说明实现主体的媒体类型。
         * Content-Length，说明实现主体的大小。
         * Connection，连接管理，可以是Keep-Alive或close。
         * 
        */
        char m_real_file[FILENAME_LEN]; //存储读取文件的名称的数组
        char *m_url;                    //url
        char *m_version;                //http版本
        char *m_host;                   //host
        int m_content_length;           //Content-Length
        bool m_linger;                  //


        char *m_file_address;           //读取服务器上的文件地址
        struct stat m_file_stat;        //
        struct iovec m_iv[2];           ////io向量机制iovec
        int m_iv_count;
	    int cgi;                        //是否启用的POST
        char *m_string;                 //存储请求头数据
};
#endif
