/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-07-13 17:28:38
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-07-25 11:51:00
 */
/**
 * 多线程同步，确保任一时刻只能有一个线程能进入关键代码段.
    > * 信号量
    > * 互斥锁
    > * 条件变量
 * 线程同步的三种方式：互斥量、条件变量和信号量
 *  
 *  1、RAII  资源获取即初始化，C++里就是类的构造函数里申请资源，析构函数里释放资源
 *  2、信号量 信号量是一种特殊的变量，P/V两种操作
 *          P，如果SV的值大于0，则将其减一；若SV的值为0，则挂起执行
 *          V，如果有其他进程因为等待SV而挂起，则唤醒；若没有，则将SV值加一
 *      sem_init函数用于初始化一个未命名的信号量
 *      sem_destory函数用于销毁信号量
 *      sem_wait函数将以原子操作方式将信号量减一,信号量为0时,sem_wait阻塞
 *      sem_post函数以原子操作方式将信号量加一,信号量大于0时,唤醒调用sem_post的线程
 *  3、互斥量
 *      pthread_mutex_init函数用于初始化互斥锁
 *      pthread_mutex_destory函数用于销毁互斥锁
 *      pthread_mutex_lock函数以原子操作方式给互斥锁加锁
 *      pthread_mutex_unlock函数以原子操作方式给互斥锁解锁
 *  4、条件变量
 *      pthread_cond_init函数用于初始化条件变量
 *      pthread_cond_destory函数销毁条件变量
 *      pthread_cond_broadcast函数以广播的方式唤醒所有等待目标条件变量的线程
 *      pthread_cond_wait函数用于等待目标条件变量.
 *          该函数调用时需要传入 mutex参数(加锁的互斥锁) ,函数执行时,
 *          先把调用线程放入条件变量的请求队列,然后将互斥锁mutex解锁,
 *          当函数成功返回为0时,互斥锁会再次被锁上. 
 *          也就是说函数内部会有一次解锁和加锁操作.
*/
#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>


/**
 * 信号量 信号量是一种特殊的变量，P/V两种操作
*          P，如果SV的值大于0，则将其减一；若SV的值为0，则挂起执行
*          V，如果有其他进程因为等待SV而挂起，则唤醒；若没有，则将SV值加一
*      sem_init函数用于初始化一个未命名的信号量
*      sem_destory函数用于销毁信号量
*      sem_wait函数将以原子操作方式将信号量减一,信号量为0时,sem_wait阻塞
*      sem_post函数以原子操作方式将信号量加一,信号量大于0时,唤醒调用sem_post的线程
*/
class sem{
    public:
        //构造即初始化
        sem()
        {
            //信号量初始化
            //sem_init函数用于初始化一个未命名的信号量
            if(sem_init(&m_sem,0,0)!=0){
                throw std::exception();
            }
        }
        //析构即释放
        ~sem()
        {
            //sem_destory函数用于销毁信号量
            sem_destroy(&m_sem);
        }
        //等待函数
        bool wait()
        {
            //sem_wait函数将以原子操作方式将信号量减一,信号量为0时,sem_wait阻塞
            return sem_wait(&m_sem)==0;
        }
        //
        bool post()
        {
            //sem_post函数以原子操作方式将信号量加一,信号量大于0时,唤醒调用sem_post的线程
            return sem_post(&m_sem)==0;
        }
    private:
        sem_t m_sem;    //信号量
};

/**
 * 互斥量
 *      pthread_mutex_init函数用于初始化互斥锁
 *      pthread_mutex_destory函数用于销毁互斥锁
 *      pthread_mutex_lock函数以原子操作方式给互斥锁加锁
 *      pthread_mutex_unlock函数以原子操作方式给互斥锁解锁
*/
class locker
{
    public:
        //构造即初始化
        locker()
        {
            //pthread_mutex_init函数用于初始化互斥锁
            if(pthread_mutex_init(&m_mutex,NULL)!=0)
            {
                throw std::exception();
            }
        }

        //析构即释放
        ~locker()
        {
            //pthread_mutex_destory函数用于销毁互斥锁
            pthread_mutex_destroy(&m_mutex);
        }
        
        bool lock()
        {
            //pthread_mutex_lock函数以原子操作方式给互斥锁加锁
            return pthread_mutex_lock(&m_mutex)==0;
        }
        bool unlock()
        {
            //pthread_mutex_unlock函数以原子操作方式给互斥锁解锁
            return pthread_mutex_unlock(&m_mutex)==0;
        }
    private:
        pthread_mutex_t m_mutex;
};


/**条件变量
 * 
 * pthread_cond_init函数用于初始化条件变量
 * pthread_cond_destory函数销毁条件变量
 * pthread_cond_broadcast函数以广播的方式唤醒所有等待目标条件变量的线程
 * pthread_cond_wait函数用于等待目标条件变量.
 *    该函数调用时需要传入 mutex参数(加锁的互斥锁) ,函数执行时,
 *    先把调用线程放入条件变量的请求队列,然后将互斥锁mutex解锁,
 *    当函数成功返回为0时,互斥锁会再次被锁上. 
 *    也就是说函数内部会有一次解锁和加锁操作.
*/
class cond
{
public:
    //构造即初始化，条件变量还要看一下互斥锁
    cond(){
        if(pthread_mutex_init(&m_mutex,NULL)!=0){
            throw std::exception();
        }
        if(pthread_cond_init(&m_cond,NULL)!=0){
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    //析构即释放
    ~cond(){
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }
    //wait操作，
    /*
    *    该函数调用时需要传入 mutex参数(加锁的互斥锁) ,函数执行时,
    *    先把调用线程放入条件变量的请求队列,然后将互斥锁mutex解锁,
    *    当函数成功返回为0时,互斥锁会再次被锁上. 
    *    也就是说函数内部会有一次解锁和加锁操作.
    */
    bool wait()
    {
        int ret=0;
        pthread_mutex_lock(&m_mutex);
        ret=pthread_cond_wait(&m_cond,&m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret==0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond)==0;
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
