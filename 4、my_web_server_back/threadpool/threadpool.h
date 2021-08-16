/**
 * 一、服务器主要框架为：I/O单元、逻辑处理单元、网络存储单元三部分，每个单元部分之间通过
 *      请求队列进行通信，相互协同。其中：
 *      (1)I/O处理单元用于处理客户端连接、读写网络数据；
 *      (2)逻辑处理单元用于处理业务逻辑；
 *      (3)网络存储单元指的是数据库或者存储文件。
 *
 * ****************************************
 * 
 * 二、五种I/O模型：
 *      (1)阻塞IO：比如accept函数，程序要用到某个函数，需要等待这个函数返回之后才能继续运行
 *      (2)非阻塞IO：非阻塞等待，程序中需要每隔一段时间就调用函数去检测事件是否就绪，没就绪就去干别的。
 *          非阻塞IO执行非阻塞I/O执行系统调用总是立即返回，不管事件是否已经发生，
 *          若事件没有发生，则返回-1，此时可以根据errno区分这两种情况，
 *          对于accept，recv和send，事件未发生时，errno通常被设置成eagain
 *      (3)信号驱动IO：linux用套接口进行信号驱动IO，安装一个信号处理函数，进程继续运行并不阻塞，
 *          当IO事件就绪，进程收到SIGIO信号，然后就去处理IO事件。
 *      (4)IO复用：用select/poll函数实现IO复用模型，这两个函数也会使进程阻塞，但是和阻塞IO所不同的是这两个函数可以同时阻塞多个IO操作。
 *          而且可以同时对多个读操作、写操作的IO函数进行检测。知道有数据可读或可写时，才真正调用IO操作函数
 *          epoll函数可以设置触发方式，LT或者ET，如果是LT触发也是同步的，ET的话就是异步。
 *      (5)异步IO：linux中，可以调用aio_read函数告诉内核描述字缓冲区指针和缓冲区的大小、文件偏移及通知的方式，
 *          然后立即返回，当内核将数据拷贝到缓冲区后，再通知应用程序。
 *      注意：阻塞I/O，非阻塞I/O，信号驱动I/O和I/O复用都是同步I/O。
 *      同步I/O指内核向应用程序通知的是就绪事件，比如只通知有客户端连接，要求用户代码自行执行I/O操作，
 *      异步I/O是指内核向应用程序通知的是完成事件，比如读取客户端的数据后才通知应用程序，由内核完成I/O操作，应用程序只需要处理逻辑关系。
 * 
 * ****************************************
 * 
 * 三、事件处理模式
 *      (1)reactor模式中，主线程(I/O处理单元)只负责监听文件描述符上是否有事件发生，有的话立即通知工作线程(逻辑单元 )
 *          ，读写数据、接受新连接及处理客户请求均在工作线程中完成。通常由同步I/O实现。
 *      (2)proactor模式中，主线程和内核负责处理读写数据、接受新连接等I/O操作，
 *          工作线程仅负责业务逻辑，如处理客户请求。通常由异步I/O实现。
 *  
 * ****************************************
 * 
 * 四、同步I/O模拟proactor模式
 * 由于异步I/O并不成熟，实际中使用较少，这里将使用同步I/O模拟实现proactor模式。
 * 同步I/O模型的工作流程如下（epoll_wait为例）：
 *      (1)主线程往epoll内核事件表注册socket上的读就绪事件。
 *      (2)主线程调用epoll_wait等待socket上有数据可读
 *      (3)当socket上有数据可读，epoll_wait通知主线程,主线程从socket循环读取数据，
 *         直到没有更多数据可读，然后将读取到的数据封装成一个请求对象并插入请求队列。
 *      (4)睡眠在请求队列上某个工作线程被唤醒，它获得请求对象并处理客户请求，
 *         然后往epoll内核事件表中注册该socket上的写就绪事件
 *      (5)主线程调用epoll_wait等待socket可写。
 *      (6)当socket上有数据可写，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果。
 * 总体流程：主线程(读事件)->epoll_wait等待有数据可读->主线程从socket循环读数据->将数据封装并插入请求队列->
 * ->唤醒某个工作线程，工作线程从请求队列里拿到数据开始读取并处理->工作线程将结果写出来->
 * ->主线程调用epoll_wait等待工作线程写结果，等到之后将结果返回
 *   
 * ****************************************
 * 
 * 五、并发编程模式
 * 并发编程方法的实现有多线程和多进程两种，但这里涉及的并发模式
 * 指I/O处理单元与逻辑单元的协同完成任务的方法。一般有三种：
 *      (1)半同步/半异步模式
 *      (2)领导者/追随者模式
 *      (3)半同步/半反应堆模式
 * 我们主要采用的是半同步半反应堆模式:半同步/半反应堆并发模式是半同步/半异步的变体，将半异步具体化为某种事件处理模式.
 *      其实应该叫半反应堆/半同步，因为主线程采用的是reactor那种
 * 知识点解释：
 *     1、并发模式中的同步和异步
 *      (1)同步指的是程序完全按照代码序列的顺序执行
 *      (2)异步指的是程序的执行需要由系统事件驱动
 *     2、半同步/半异步模式工作流程(我觉得应该叫半异步/半同步，主线程异步，工作线程同步)
 *      (1)同步线程用于处理客户逻辑
 *      (2)异步线程用于处理I/O事件
 *      (3)异步线程监听到客户请求后，就将其封装成请求对象并插入请求队列中
 *      (4)请求队列将通知某个工作线程在同步模式的工作线程来读取并处理该请求对象(其实，这时候已经确认有事件，同步并无大碍)
 *     3、半同步/半反应堆工作流程（正常应该是reactor模式，但是此处模拟出Proactor模式）
 *      (1)主线程充当异步线程，负责监听所有socket上的事件
 *      (2)若有新请求到来，主线程接收之以得到新的连接socket(accept嘛，会产生连接套接字)，
 *          然后往epoll内核事件表中注册该socket(此时是连接套接字)上的读写事件
 *      (3)如果连接socket上有读写事件发生，主线程从socket上接收数据，并将数据封装成请求对象插入到请求队列中
 *      (4)所有工作线程睡眠在请求队列上，当有任务到来时，通过竞争（如互斥锁）获得任务的接管权
 *   
 * ****************************************
 * 
 * 六、线程池
 * 说白了，就是用空间换时间,浪费服务器的硬件资源,换取运行效率.
 * 池是一组资源的集合,这组资源在服务器启动之初就被完全创建好并初始化,这称为静态资源.
 * (1)当服务器进入正式运行阶段,开始处理客户请求的时候,如果它需要相关的资源,可以直接从池中获取,
 * 无需动态分配.
 * (2)当服务器处理完一个客户连接后,可以把相关的资源放回池中,无需执行系统调用释放资源.
 * 
 * 半同步/半反应堆线程池
 * 本项目中，线程池的设计模式为半同步/半反应堆，其中反应堆具体为Proactor事件处理模式。
 *     (1)主线程为异步线程，负责监听文件描述符，接收socket新连接，若当前监听的socket发生了读写事件，
 *      然后将任务插入到请求队列。
 *     (2)工作线程从请求队列中取出任务，完成读写数据的处理。  
 * 
 * 
 * ****************************************
 * 
 * 七、程序基础知识
 * 
 * 1、静态成员变量
 *  (1)将类成员变量声明为static，则为静态成员变量，与一般的成员变量不同，
 *  无论建立多少对象，都只有一个静态成员变量的拷贝，静态成员变量并不属于某一个类，而是所有对象共享。
 *  (2)静态变量在编译阶段就分配了空间，对象还没创建时就已经分配了空间，放到全局静态区。
 *  (3)静态成员变量最好是类内声明，类外初始化（以免类名访问静态成员访问不到）。
 *  (4)无论公有，私有，静态成员都可以在类外定义，但私有成员仍有访问权限。
 *  (5)非静态成员类外不能初始化。
 *  (6)静态成员数据是共享的。
 * 
 * 2、静态成员函数
 *  (1)将类成员函数声明为static，则为静态成员函数。
 *  (2)静态成员函数可以直接访问静态成员变量，不能直接访问普通成员变量，但可以通过参数传递的方式访问。
 *  (3)普通成员函数可以访问普通成员变量，也可以访问静态成员变量。
 *  (4)因为静态成员函数没有this指针。非静态数据成员为对象单独维护，但静态成员函数为共享函数，无法区分是哪个对象，因此不能直接访问普通变量成员，也没有this指针。
 * 
 * 3、pthread_create陷阱！！！！！！！！！！！！
 *  首先看一下该函数的函数原型。
 *      #include <pthread.h>
 *      int pthread_create (pthread_t *thread_tid,                 //返回新生成的线程的id
 *                           const pthread_attr_t *attr,         //指向线程属性的指针,通常设置为NULL
 *                           void * (*start_routine) (void *),   //处理线程函数的地址
 *                           void *arg);                         //start_routine()中
 *  函数原型中的第三个参数，为函数指针，指向处理线程函数的地址。就是创建一个线程，该线程要处理的那个函数的指针
 *  该函数，要求为静态函数。
 *  所以！！！！！！如果处理线程函数为类成员函数时，需要将其设置为静态成员函数。
 *  因为下面三行描述：
 *  pthread_create的函数原型中第三个参数的类型为函数指针，指向的线程处理函数参数类型为(void *),
 *  若线程函数为类成员函数，则this指针会作为默认的参数被传进函数中，从而和线程函数参数(void*)不能匹配，不能通过编译。
 *  所以，用静态成员函数就没有这个问题，里面没有this指针。
 * 
 
    
*/
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include"../lock/locker.h"


//线程池的类
//需要注意：
//要把<线程处理函数>和<运行函数>设置为私有属性。
//采用模板类，增加可复用性
template<typename T>
class threadpool{
    public:
        //(1)线程池构造函数
        threadpool(int thread_number=8,//线程池中线程的数量
                    int max_request=10000);//max_requests是请求队列中最多允许的、等待处理的请求的数量
        //(2)线程析构函数
        ~threadpool();

        //(3)向请求队列中插入任务请求
        bool append(T* request);

    private:
        //工作线程运行的函数，它不断从工作队列中取出任务并执行之
        static void *worker(void *arg);//一定一定要设置成static，因为不然会把this指针带出去
        
        //线程池运行函数
        void run();


        
    private:

        //线程池中的线程数
        int m_thread_number;

        //请求队列中允许的最大请求数
        int m_max_requests;

        //描述线程池的数组，其大小为 m_thread_number
        pthread_t *m_threads;

        //请求队列
        std::list<T *> m_workqueue;

        //保护请求队列的互斥锁
        locker m_queuelocker;

        //是否有任务需要处理，也可以采用生产者消费者模型的条件变量
        sem m_queuestat;
        
        //是否结束线程
        bool m_stop;
};


//线程池构造函数，创建线程池
template<typename T>
threadpool<T>::threadpool(int thread_number,
                          int max_requests)
                :m_thread_number(thread_number),//初始化列表对类成员变量进行初始化
                 m_max_requests(max_requests),
                 m_stop(false),
                 m_threads(NULL){//m_threads代表线程池数组的首地址
    
    //1、传入参数异常检测：最大线程数小于等于0或者最大请求队列数小于等于0
    if(thread_number<=0||max_requests<=0)
        throw std::exception();

    //2、new一个线程组，大小是线程数那么大  就是8个
    m_threads=new pthread_t[m_thread_number];
    // 检测一下成功了吗
    if(!m_threads)
        throw std::exception();
    
    //3、遍历线程池
    for(int i=0;i<thread_number;++i)
    {
        printf("create the %dth thread\n",i);
        //循环创建线程，并将工作线程按要求进行运行，并异常检测
        //这里pthread_create 传入的参数回调函数的参数 为this
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete [] m_threads;
            throw std::exception();
        }
        //将线程进行分离后，不用单独对工作线程进行回收，并异常检测
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

//线程池销毁，记得用delete[]
//不用删除任务队列吗
template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop=true;
}


//通过list容器创建请求队列，同时，向队列中添加请求任务时，通过互斥锁保证线程安全，
//添加完成后通过信号量提醒有任务要处理，最后注意线程同步。
template<typename T>
bool threadpool<T>::append(T* request)
{
    //加锁
    m_queuelocker.lock();

    //根据硬件，预先设置请求队列的最大值
    if(m_workqueue.size()>m_max_requests)
    {
        //时刻记得，判断中return的话要解锁
        m_queuelocker.unlock();
        return false;
    }
    //如果m_workqueue.size() <= m_max_requests添加请求任务
    m_workqueue.push_back(request);
    //添加完要解锁
    m_queuelocker.unlock();

    //用信号量提醒有任务要处理
    m_queuestat.post();
    return true;
}


//内部访问私有成员函数run，完成线程处理要求。
template<typename T>
void* threadpool<T>::worker(void* arg){
    
    //arg是线程池类的this
    //所以将参数强转为线程池类类型，调用其成员方法
    threadpool* pool=(threadpool*)arg;
    pool->run();
    return pool;
}


//工作线程从请求队列中取出某个任务进行处理，注意线程同步。
template<typename T>
void threadpool<T>::run()
{
    //只要线程池没有停止
    while(!m_stop)
    {
        //阻塞等待信号量
        m_queuestat.wait();

        //等到信号，代表可以连接了，线程唤醒
        // 加锁
        m_queuelocker.lock();
        //发现没任务 继续睡觉
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        //否则取出任务指针 m_workqueue.front()
        T* request=m_workqueue.front();
        m_workqueue.pop_front();
        // 解锁
        m_queuelocker.unlock();
        if(!request)
            continue;
        //处理任务
        request->process();
    }
}
#endif

