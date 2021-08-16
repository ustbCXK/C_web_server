/**
 * 定时器处理非活动连接
    ===============
    由于非活跃连接占用了连接资源，严重影响服务器的性能，
    通过实现一个服务器定时器，处理这种非活跃连接，释放连接资源。
    具体，利用 alarm 函数周期性地触发 SIGALRM 信号,
    该信号的 <信号处理函数> 利用管道通知主循环执行<定时器链表>上的<定时任务>.
    > * <统一事件源>
    > * <基于升序链表的定时器>
    > * <处理非活动连接>
 * 
 * 在本项目中，
 * 1、服务器主循环为每一个连接都创建一个定时器；
 * 2、利用创建的定时器对每个连接进行定时；
 * 3、利用升序时间链表容器 将所有定时器串联起来； 注意，这里是按照结束时间升序
 * 4、若主循环接收到定时通知，则在链表中依次执行定时任务。
 * 
 * Linux下有三种定时方法：socket选项SO_RECVTIMEO和SO_SNDTIMEO、SIGALRM信号、IO复用系统调用的超时参数
 * 本项目中采用SIGALRM信号，具体来说就是利用alarm函数周期性出发SIGALRM信号，
 * 信号处理函数利用 <管道> 通知主循环，主循环对升序链表上的所有定时器进行处理，
 * 如果发现有不干活的套接字，就关闭，释放资源。
 * 所以主要可以分为两个部分：
 *      1、定时方法与信号通知流程；
 *      2、定时器及其容器设计与定时任务的处理。
 * 
 *************************************************
 * 
 * 一、基础API
 * 描述sigaction结构体
 * sigaction 函数:sigaction () 函数和 signal () 函数的功能是一样的，用于捕捉进程中产生的信号，
 *                并将用户自定义的信号行为函数（回调函数）注册给内核，内核在信号产生之后调用这个处理动作。
 * sigfillset 函数 ：// 将set集合中所有的标志位设置为x
 * SIGALRM 信号
 * SIGTERM 信号
 * alarm 函数 ：定时函数
 * socketpair 函数
 * send函数：当套接字发送缓冲区变满时，send通常会阻塞，除非套接字设置为
 *          非阻塞模式，当缓冲区变满时，返回EAGAIN或者EWOULDBLOCK错误，
 *          此时可以调用select函数来监视何时可以发送数据。
 * 
 *************************************************
 * 
 * 二、信号通知流程
 * 
 * Linux下的信号，等同于一个中断，但是在执行信号处理函数期间，对其他信号是屏蔽的
 * 所以，本项目中，信号处理函数仅仅发送信号通知<主循环>,将信号对应的处理逻辑(定时)放
 * 在主循环中，由主循环执行。
 * 
 * 
 * 信号通知过程：(统一事件源)
 *  1、创建管道，其中信号处理函数往管道写端写入信号值，主循环从管道读端通过I/O复用系统监测读事件(这里设置成ET的)
 *     (统一事件源)是指信号事件与其他文件描述符都可以通过epoll来监测，从而实现统一处理。
 *  2、设置信号处理函数 SIGALRM （时间到了触发）和 SIGTERM （kill会触发，Ctrl+C）
 *      2.1 通过 struct sigaction 结构体和 sigaction 函数注册信号捕捉函数
 *      2.2 在结构体的 handler 参数(信号捕捉到之后的处理动作，这是一个函数指针)设置信号处理函数，具体的，从管道写端写入信号的名字
 *  3、利用 I/O复用系统 监听管道 读端文件描述符 的可读事件(信号)
 *  4、信息值传递给主循环，主循环再根据接收到的信号值执行目标信号对应的逻辑代码
 * 
 *************************************************
 * 
 * 三、定时器设计、容器设计、定时任务处理函数和使用定时器。
 *  
 * 1、定时器设计：将连接资源和定时事件等封装成定时器类，具体包括：
 *       (1)连接资源：客户端套接字地址、文件描述符和定时器
 *       (2)超时时间:浏览器和服务器连接时刻 + 固定时间(TIMESLOT)
 *          定时器使用绝对时间作为超时值，这里alarm设置为5秒，连接超时为15秒。
 *       (3)回调函数:指向定时事件->删除非活动socket上的注册事件，并关闭
 *  
 * 2、定时器容器设计：将多个定时器串联组织起来统一处理，具体包括升序链表设计。
 *     为每个连接创建一个定时器，将其添加到链表中，并按照<超时时间>升序排列。
 *      执行定时任务时，将到期的定时器从链表中删除：主要是链表插入和删除两操作。
 *      2.1 创建头尾节点，其中头尾节点没有意义，仅仅统一方便调整
 *      2.2 add_timer 函数，将目标定时器添加到链表中，添加时按照升序添加
 *          (1)若当前链表中只有头尾节点，直接插入；
 *          (2)否则，将定时器按升序插入
 *      2.3 adjust_timer 函数，当定时任务发生变化,调整对应定时器在链表中的位置
 *          (1)客户端在设定时间内有数据收发,则重新设定该定时器的时间，
 *              只是往后延长超时时间
 *          (2)被调整的目标定时器在尾部，或定时器新的超时值仍然小于下一个
 *              定时器的超时，不用调整   
 *          (3)否则先将定时器从链表取出，重新插入链表
 *      2.4 del_timer 函数将超时的定时器从链表中删除,常规链表节点删除
 * 
 *  3、定时任务处理函数：该函数封装在容器类中，具体的，函数遍历升序链表容器，
 *       根据超时时间，处理对应的定时器。使用统一事件源，SIGALRM信号每次被触发，主循环中调用一次定时任务处理函数，处理链表容器中到期的定时器。
 *  具体的逻辑如下:
 *      (1)遍历定时器升序链表容器，从头结点开始依次处理每个定时器，直到遇到尚未到期的定时器;
 *      (2)若当前时间小于定时器超时时间，跳出循环，即未找到到期的定时器
 *      (3)若当前时间大于定时器超时时间，即找到了到期的定时器，执行回调函数，然后将它从链表中删除，然后继续遍历
 * 
 *  4、项目中使用定时器的方案：
 *      服务器首先创建定时器容器链表，然后用统一事件源将异常事件，读写事件和
 *  信号事件统一处理，根据不同事件的对应逻辑使用定时器。
 *  
 *  具体的逻辑如下:
 *      (1)浏览器与服务器连接时，创建该连接对应的定时器，并将该定时器添加到链表上;
 *      (2)处理异常事件(异常导致关闭，顺便删掉定时器)时，执行定时事件，服务器
 *         关闭连接，从链表上移除对应定时器;
 *      (3)处理定时信号时，将定时标志设置为true
 *      (4)处理读事件时，若某连接上发生读事件，将对应定时器向后移动，
 *         否则，执行定时事件
 *      (5)处理写事件时，若服务器通过某连接给浏览器发送数据，将对应定时器向后移动，
 *         否则，执行定时事件
 * 
 * 
 *************************************************
*/
#ifndef LST_TIMER
#define LST_TIMER



#include <time.h>
#include "../log/log.h"

#define BUFFER_SIZE 64
//连接资源结构体成员 需要用到的定时器类
//需要前向声明
class util_timer;

//连接资源，每个新的连接都有的数据，更多的是用来定时的
struct client_data
{
    //客户端socket地址地址，没咋用到，可以加点东西，比如检测异地登录啥的
    //TODO：
    sockaddr_in address;
    //socket，连接的文件描述符
    int sockfd;
    //加了一个缓冲区
    char buf[ BUFFER_SIZE ];
    //定时器
    util_timer* timer;
};

//自定义的定时器类
class util_timer
{
public:
    util_timer() : prev( NULL ), next( NULL ){}

public:
    //超时时间
    time_t expire;
    //回调函数，参数为连接资源
    void (*cb_func)( client_data* );
    //连接资源
    client_data* user_data;
    //前向定时器
    util_timer* prev;
    //后向定时器
    util_timer* next;
};



//定时器容器类，双向链表
class sort_timer_lst
{
public:
    sort_timer_lst() : head( NULL ), tail( NULL ) {}
    //常规销毁链表
    ~sort_timer_lst()
    {
        //delete掉每一个节点
        util_timer* tmp = head;
        while( tmp )
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    //添加定时器，内部调用私有成员add_timer
    void add_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }
        //链表只有一个节点，那就是新来的
        if( !head )
        {
            head = tail = timer;
            return; 
        }

        //如果新的定时器 超时时间 小于 当前头部结点
        //直接将当前定时器结点作为头部结点
        if( timer->expire < head->expire )
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        //否则调用私有成员，调整内部结点
        //TODO：为啥非要用重载？难受
        add_timer( timer, head );
    }

    //调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }
        util_timer* tmp = timer->next;

        //被调整的定时器在链表尾部
        //定时器超时值仍然小于下一个定时器超时值，不调整
        if( !tmp || ( timer->expire < tmp->expire ) )
        {
            return;
        }

        //被调整定时器是链表头结点，将定时器取出，重新插入
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            //否则调用私有成员，调整内部结点
            //TODO：为啥非要用重载？难受
            add_timer( timer, head );
        }
        //被调整定时器在内部，将定时器取出，重新插入
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            //否则调用私有成员，调整内部结点
            //TODO：为啥非要用重载？难受
            add_timer( timer, timer->next );
        }
    }

    //删除定时器
    void del_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }

        //链表中只有一个定时器，需要删除该定时器
        if( ( timer == head ) && ( timer == tail ) )
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }

        //被删除的定时器为头结点
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }

        //被删除的定时器为尾结点
        if( timer == tail )
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }

        //被删除的定时器在链表内部，常规链表结点删除
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    //定时任务处理函数
    void tick()
    {
        if( !head )
        {
            return;
        }
        //printf( "timer tick\n" );
	    LOG_INFO("%s","timer tick");
        Log::get_instance()->flush();

        //获取当前时间
        time_t cur = time( NULL );
        util_timer* tmp = head;
        //遍历定时器链表
        while( tmp )
        {
            //链表容器为升序排列
            //当前时间小于定时器的超时时间，后面的定时器也没有到期
            if( cur < tmp->expire )
            {
                break;
            }
            //否则的话，那就是不小于定时器的超时时间嘛
            //当前定时器到期，则调用回调函数，执行定时事件
            tmp->cb_func( tmp->user_data );

            //将处理后的定时器从链表容器中删除，并重置头结点
            head = tmp->next;
            if( head )
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    // 私有成员，被公有成员 add_timer 和 adjust_time 调用
    // 主要用于调整链表内部结点
    void add_timer( util_timer* timer, util_timer* lst_head )
    {
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;

        //遍历当前结点之后的链表，
        //按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
        while( tmp )
        {
            if( timer->expire < tmp->expire )
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        //遍历完发现，目标定时器需要放到尾结点处
        if( !tmp )
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
        
    }

private:
    //头尾结点
    util_timer* head;
    util_timer* tail;
};

#endif
