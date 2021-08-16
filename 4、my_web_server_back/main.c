#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"

#define MAX_FD 65536		//最大文件描述符
#define MAX_EVENT_NUMBER 10000	//最大事件数
#define TIMESLOT 5		//最小超时单位

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd,int fd,bool one_shot);
extern int removefd(int epollfd,int fd);
extern int setnonblocking( int fd );

//设置定时器相关参数
static int pipefd[2];//管道，是用来传递信号量的
//创建定时器容器链表
static sort_timer_lst timer_lst;

//epoll套接字
static int epollfd = 0;

//信号处理函数(当主循环通过epoll检测到内核通过管道传递来的信号时，
//会执行的一个函数，在这执行是为了避免占用信号发送进程的时间)
void sig_handler( int sig )
{
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入，传输字符类型，而非整型
    send( pipefd[1], ( char* )&msg, 1, 0 );

    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

//设置信号函数
void addsig(int sig,void(handler)(int),bool restart=true)
{
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));

    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    //执行 sigaction 函数
    assert(sigaction(sig,&sa,NULL)!=-1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    //定时任务处理函数
    timer_lst.tick();
    //重新定时
    alarm( TIMESLOT );
}

//定时器回调函数，执行这个函数的定时器对应的连接 都超时了，所以需要关闭 
//删除非活动连接在socket上的注册事件，并关闭
void cb_func( client_data* user_data )
{
    //删除非活动连接在socket上的注册事件
    epoll_ctl( epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0 );
    assert( user_data );
    //关闭文件描述符
    close( user_data->sockfd );
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
    //printf( "close fd %d\n", user_data->sockfd );
    //应该减少一个连接数的
    //TODO：应该减少一个连接数的
}

void show_error(int connfd,const char* info)
{
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}

//设置信号为LT阻塞模式
void addfd_(int epollfd,int fd,bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLRDHUP;
    if(one_shot)
        event.events|=EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
}



int main(int argc,char *argv[])
{
    
    //1、日志系统，单例模式就足够了
    //Log::get_instance()->init("./mylog.log",8192,2000000,10);//异步日志模型
    Log::get_instance()->init("./mylog.log",8192,2000000,0);//同步日志模型

    if(argc<=1)
    {
        printf("usage: %s ip_address port_number\n",basename(argv[0]));
        return 1;
    }
    //2、端口设置
    //const char* ip=argv[1];
    int port=atoi(argv[1]);
    
    //忽略SIGPIPE信号
    //SIG_IGN ：忽略的处理方式，子进程状态信息会被丢弃，自动回收，所以不会产生僵尸进程
    addsig(SIGPIPE,SIG_IGN);

    //3、创建线程池，模板参数T给定 http_conn类实例
    threadpool<http_conn>* pool=NULL;
    try
    {
        pool=new threadpool<http_conn>;
    }
    catch(...){
        return 1;
    }

    //4、单例模式创建数据库连接池
    connection_pool *connPool=connection_pool::GetInstance("localhost","root","root","qgydb",3306,5);    

    //5、创建MAX_FD 个 http类对象，65536个
    http_conn* users=new http_conn[MAX_FD];
    assert(users);
    int user_count=0;
    //初始化http类的数据库读取表
    users->initmysql_result();

    //6、创建套接字，返回listenfd
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd>=0);
    //struct linger tmp={1,0};

    //SO_LINGER若有数据待发送，延迟关闭
    //setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    //7、绑定地址
    int ret=0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    //inet_pton(AF_INET,ip,&address.sin_addr);
    //字节序转换
    address.sin_addr.s_addr=htonl(INADDR_ANY);
    address.sin_port=htons(port);

    //8、设置端口复用，绑定端口
    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret=bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    //printf("bind ret = %d\n", ret);
    assert(ret>=0);
    //9、监听端口
    ret=listen(listenfd,5); 
    assert(ret>=0);
    
    //10、epoll初始化创建内核事件表  最大值10000个
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);
    assert(epollfd!=-1);
    //<挂到epollfd上去>,设置信号为LT阻塞模式
    //one_shot = false 代表不用one_shot
    //警告！！这是监听套接字，不能设置one_shot，否则应用会只处理一个客户端连接，
    //因为后续的客户端连接 将不会触发epollfd 上的EPOLLIN事件
    addfd_(epollfd,listenfd,false);
    http_conn::m_epollfd = epollfd;

    //创建管道
    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    //socketpair()函数用于创建一对无名的、相互连接的套接字。 
    //  如果函数成功，则返回0，创建好的套接字分别是pipefd[0]读端和pipefd[1]写端；
    //  这对套接字可以用于全双工通信，每一个套接字既可以读也可以写。
    //  否则返回-1，错误码保存于errno中。
    assert( ret != -1 );
    // 设置管道写端为非阻塞，为什么写端要非阻塞？
    //  答案：因为send是将信息发送给套接字缓冲区，
    //  如果缓冲区满了，则会阻塞，这时候会进一步增加信号处理函数的执行时间，
    //  为此，将其修改为非阻塞。但是，如果设置为非阻塞的话，那么有可能会导致某次定时时间丢失
    setnonblocking( pipefd[1] );
    //将 pipefd[0]管道读端 挂到epollfd  还是ET模式 不能设置oneshot
    addfd( epollfd, pipefd[0], false);

    //传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    //循环条件
    bool stop_server = false;

    //新建客户端类 65536个 //创建连接资源数组，其中包含定时器用来定时的
    client_data* users_timer = new client_data[MAX_FD]; 
    //超时标志位默认为False
    bool timeout = false;
    // alarm()函数的主要功能是设置信号传送闹钟，
    //  即用来设置信号SIGALRM在经过参数TIMESLOT秒数后发送给目前的进程。
    alarm( TIMESLOT );


    //printf("监听......\n");
    while(!stop_server)
    {
        // number返回有多少文件描述符就绪
        // 时间到了就返回0
        int number=epoll_wait(epollfd,//用来存内核得到事件的集合，
                              events,//内核事件表
                              MAX_EVENT_NUMBER,// 告之内核这个events有多大，这个maxevents的值不能大于创建
                              -1);
        
        if(number<0&&errno!=EINTR)
        {
            //printf("epoll failure\n");
	        LOG_ERROR("%s","epoll failure");
            break;
        }

        //遍历所有的events的文件描述符
        for(int i=0;i<number;i++)
        {
            //sockfd 是连接套接字  如果要用ET模式，就必须设置为非阻塞并且oneshot
            //处理新到的客户连接
            int sockfd=events[i].data.fd;

	        //处理新到的客户连接
            if(sockfd==listenfd)
            {
                //初始化客户端连接地址
                //TODO：可以增加新功能哦！
                struct sockaddr_in client_address;
                socklen_t client_addrlength=sizeof(client_address);
                
                // 接受http连接请求生成连接套接字
                // connfd---->该连接分配的文件描述符
                int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
                if(connfd<0)
                {
                    //printf("errno is:%d\n",errno);
		            LOG_ERROR("%s:errno is:%d","accept error",errno);
                    continue;
                }
                if(http_conn::m_user_count>=MAX_FD)
                {
                    show_error(connfd,"Internal server busy");
		            LOG_ERROR("%s","Internal server busy");
                    continue;
                }
                //对上述类对象进行初始化
                users[connfd].init(connfd,client_address);
		
                //初始化client_data数据
                //(1)创建定时器，设置回调函数和超时时间，绑定用户数据，
                //(2)将定时器添加到链表中
                //  初始化该连接对应的连接资源
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                //创建定时器临时变量
                util_timer* timer = new util_timer;
                //设置定时器对应的连接资源
                timer->user_data = &users_timer[connfd];
                //设置回调函数
                timer->cb_func = cb_func;

                time_t cur = time( NULL );
                //设置绝对超时时间
                timer->expire = cur + 3 * TIMESLOT;
                //创建该连接对应的定时器，初始化为前述临时变量
                users_timer[connfd].timer = timer;
                //将该定时器添加到链表中
                timer_lst.add_timer( timer );
            }
            //否则是 连接套接字 出现断开 或者异常 就去除该连接
	        else if(events[i].events & (EPOLLRDHUP | EPOLLHUP|EPOLLERR))
            {
                users[sockfd].close_conn();

                //服务器端关闭连接，移除对应的定时器
                cb_func( &users_timer[sockfd] );
                util_timer *timer=users_timer[sockfd].timer;
                if( timer )
                {
                    timer_lst.del_timer( timer );
                }
            }
	        //有信号产生，处理信号，优先级比读写高
            //套接字是管道读，并且发生的是发生读事件，也就是有信号量传过来了
	        else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                 //接收到SIGALRM信号，timeout设置为True
                int sig;
                char signals[1024];
                //从管道读端读出信号值，成功返回字节数，失败返回-1
                //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 )
                {
                    // handle the error
                    continue;
                }
                else if( ret == 0 )
                {
                    continue;
                }
                else
                {
                    //处理信号值对应的逻辑
                    for( int i = 0; i < ret; ++i )
                    {
                        //这里面明明是字符
                        switch( signals[i] )
                        {
                            //这里是整型
                            //报时信号
                            case SIGALRM:
                            {
                                //超时
                                timeout = true;
                                break;
                            }
                            //终止信号　
                            case SIGTERM:
                            {
                                //主动断开
                                stop_server = true;
                            }
                        }
                    }
                }
            }

            //处理客户连接上接收到的数据
            else if(events[i].events&EPOLLIN)
            {
                //更新连接时间
		        //创建定时器临时变量，将该连接对应的定时器取出来
                util_timer* timer = users_timer[sockfd].timer;

                if(users[sockfd].read_once()){
                    //读取到数据了，数据在read_buffer里
                    LOG_INFO("deal with the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    //若监测到读事件，将该事件放入到线程池的请求队列中
                    pool->append(users+sockfd);

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if( timer )
                        {
                            time_t cur = time( NULL );
                            timer->expire = cur + 3 * TIMESLOT;
                            //printf( "adjust timer once\n" );
                            LOG_INFO("%s","adjust timer once");
                            Log::get_instance()->flush();
                            timer_lst.adjust_timer( timer );
                        }
                }
                else//没读到数据
                {
                    //服务器端关闭连接，移除对应的定时器
                    users[sockfd].close_conn();
		            cb_func( &users_timer[sockfd] );
                    if( timer )
                    {
                        timer_lst.del_timer( timer );
                    }
                }
            }
            //写数据
            //events[i].events&EPOLLOUT 代表可以写了
            else if(events[i].events&EPOLLOUT)
            {
                //写成功没事，没写成功就断开了
                if(!users[sockfd].write())
                    users[sockfd].close_conn();
            }
            //else
            //{
            //}
	    
        }
        //处理定时器为非必须事件，收到信号并不是立马处理
        //完成读写事件后，再进行处理
        if(timeout){
            timer_handler();
            timeout=false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users;
    delete [] users_timer;
    delete pool;
    //销毁数据库连接池
    connPool->DestroyPool();
    return 0;
}
                
