
/**
 * 同步/异步日志系统主要涉及了两个模块:
 *      一个是日志模块;
 *      一个是阻塞队列模块,其中加入阻塞队列模块主要是解决异步写入日志做准备.
    > * 自定义阻塞队列
    > * 单例模式创建日志
    > * 同步日志
    > * 异步日志
    > * 实现按天、超行分类
 ****************************************************
 * 
 * 一、基础知识：
 *  1、日志：由服务器自动创建，并记录运行状态、错误信息、访问数据等的文件；
 *  2、同步日志：日志写入函数与工作线程串行执行，由于涉及到IO操作，所以如果
 *         单条日志过大，同步模式下会阻塞整个处理流程，服务器所能处理的并发
 *          能力有所下降，尤其是峰值的提升，日志系统会成为系统的缺陷。
 *  3、异步日志：将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出日志，
 *          写入日志；
 *  4、生产者-消费者模型：并发编程中的经典模型，以多线程为例，为实现线程数据
 *          同步，生产者和消费者共享同一个缓冲区，其中生产者线程往缓冲区中
 *          push消息通知消费者，消费者从队列中pop消费者
 *  5、阻塞队列：将生产者-消费者模型进行封装，使用循环数组实现队列，作为两者
 *          共享的缓冲区
 *  6、单例模式：最简单也是最常用的设计模式之一，保证每个类只能创建一个实例，
 *          同时提供全局访问的方法。
 * 
 ***************************************************
 * 
 * 二、重点预备知识
 *  (一) 单例模式
 *  单例模式是最简单也是最常用的设计模式之一，保证每个类只能创建一个实例，
 * 同时提供全局访问的方法。该实例被所有程序模块共享。
 * 
 * 1、实现思路：
 *  (1)私有化其构造函数，防止外接创建单例类的对象
 *  (2)使用类的私有<静态>指针变量 指向类的唯一实例，并用一个共有的静态方法
 *      获取该实例。
 *  单例模式有两种实现方法，可以大致分为懒汉模式和饿汉模式。懒汉模式，即不用
 * 的时候不去初始化，第一次使用才初始化；饿汉模式，程序一运行就初始化。
 * 
 * 2、例子，懒汉模式和饿汉模式的实现
 *  (1)经典的线程安全懒汉模式(实现思路与上述一致，不过使用双检测锁模式)
 *      ！！双检测的原因：因为如果只检测一次，在每次调用获取实例的方法时，都
 * 需要加锁，严重影响程序性能。双检测可以仅在第一次创建实例的时候加锁，其他
 * 时候不加锁，直接返回实例。
 * 
 * class single{
 * private:
 *      //私有静态指针变量指向唯一实例
 *      static single *p;
 * 
 *      //静态锁，是由于静态函数只能访问静态成员
 *      static pthread_mutex_t lock;
 *      
 *      //私有化构造函数
 *      single(){
 *          pthread_mutex_init(&lock, NULL);
 *      }
 *      ~single(){}
 * public:
 *      //公有静态方法获取实例
 *      static single* getinstance();
 * };     
 * pthread_mutex_t single::lock;
 * 
 * single* single::p = NULL;
 * single* single::getinstance(){
 *    //只会产生一个实例
 *    if (NULL == p){
 *        pthread_mutex_lock(&lock);
 *        if (NULL == p){
 *            p = new single;
 *        }
 *        pthread_mutex_unlock(&lock);
 *    }
 *    return p;
 * }
 * 
 * (2)局部静态变量之线程安全懒汉模式，双检测模式太不优雅了，使用函数内的局部
 * 静态对象，不用加锁解锁。
 * class single{
 * private:
 *    single(){}
 *    ~single(){}
 * 
 * public:
 *    static single* getinstance();
 * };
 * 
 * single* single::getinstance(){
 *    static single obj;
 *    return &obj;
 * }
 * 
 * (3)饿汉模式
 * 饿汉模式，不用加锁，线程也是安全的，因为程序运行时就定义了对象，对其初始化
 * 但是在于非静态对象（函数外的static对象）在不同编译单元中的初始化顺序是未定义的。
 * 如果在初始化完成之前调用 getInstance() 方法会返回一个未定义的实例。
 * 
 * (二)条件变量与生产者-消费者模型
 *  1、条件变量API
 * 条件变量提供了一种线程间的通知机制，当某个共享数据达到某个值，唤醒所有等数据的线程
 *      (1)pthread_cond_init函数，用于初始化条件变量
 *      (2)pthread_cond_destory函数，销毁条件变量
 *      (3)pthread_cond_broadcast函数，以广播的方式唤醒所有等待目标条件变量的线程
 *      (4)pthread_cond_wait函数，用于等待目标条件变量。
 *          外层也要加锁，因为多线程访问，避免资源竞争。
 *          该函数调用时需要传入 mutex参数(加锁的互斥锁) ，函数执行时，
 *          先把调用线程放入条件变量的请求队列，然后将互斥锁mutex解锁，
 *          当函数成功返回为0时，表示重新抢到了互斥锁，互斥锁会再次被锁上， 
 *          也就是说函数内部会有一次解锁和加锁操作.
 * 
 * 
 * 
 ***************************************************
 * 三、整体流程：
 *  本项目中，使用<单例模式>创建日志系统（因为只需要一个实体类），对服务器运行
 * 状态、错误信息和访问数据进行记录，该系统可以实现按照天、按照超行进行分类，
 * 按照实际情况分别使用同步和异步写入两种方式。其中，异步写入方式，将生产者
 * -消费者模型封装为阻塞队列，创建一个写线程，工作线程将要写的内容push进队列，
 * 写线程从队列中取出内容，写入日志文件。
 *  
 *  1、日志系统大致可以分成两部分：
 *  (1)单例模式与阻塞队列的定义，
 *  (2)日志类的定义与使用。
 * 
 * 
 ***************************************************
*/


#ifndef LOG_H  
#define LOG_H  
  
#include <stdio.h>  
#include <iostream>  
#include <string>  
#include <stdarg.h>  
#include <pthread.h>  
#include "block_queue.h"  
using namespace std;  


class Log  
{  
    public:  
        //c++11之后，使用局部变量可以不加锁
        static Log* get_instance()  
        {  
            static Log instance;  
            return &instance;  
        }  

        //异步写日志公有方法，调用私有方法 async_write_log
        static void *flush_log_thread(void* args)  
        {  
            Log::get_instance()->async_write_log();  
        } 

  	    //初始化变量
        bool init(const char* file_name, //日志文件
                  int log_buf_size = 8192, //日志缓冲区大小
                  int split_lines = 5000000, //最大行数
                  int max_queue_size = 0);  //最长日志条队列
  
        //将输出内容按照标准格式整理
        void write_log(int level, const char* format, ...);  
  
        //强制刷新缓冲区
        void flush(void);  
  
    private:  
        Log();  
        virtual ~Log();  

        //私有方法
        //异步写日志方法
        void *async_write_log()  
        {  
            string single_log; 
            
	        //从阻塞队列中取出一个日志string，写入文件
            while(m_log_queue->pop(single_log))  
            {  
                pthread_mutex_lock(m_mutex);  
                fputs(single_log.c_str(), m_fp);  
                pthread_mutex_unlock(m_mutex);  
            }  
        }  
  
    private:  
        pthread_mutex_t *m_mutex;	//互斥锁 
        char dir_name[128];  		//路径名
        char log_name[128];  		//log文件名
        int m_split_lines;  		//日志最大行数
        int m_log_buf_size;  		//日志缓冲区大小
        long long  m_count;  		//日志行数记录
        int m_today;  			//因为按天分类,记录当前时间是那一天
        FILE *m_fp;  			//打开log的文件指针
        char *m_buf;  
        block_queue<string> *m_log_queue;  //阻塞队列
        bool m_is_async;  		//是否同步标志位
};  

//__VA_ARGS__ 是一个可变参数的宏，实现思想就是宏定义中参数列表的最后一个参数为省略号（也就是三个点）
  
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)  
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)  
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)  
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, __VA_ARGS__)  
  
#endif  
