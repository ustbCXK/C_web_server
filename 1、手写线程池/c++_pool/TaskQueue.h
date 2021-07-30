/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-07-17 10:32:35
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-07-30 21:26:42
 */


// 定义任务队列结构体
#include<pthread.h>
#include<queue>

using callback = void(*)(void*);



struct Task
{
    Task()
    {
        function = nullptr;
        arg = nullptr;
    }
    Task(callback f, void* arg)
    {
        function = f;
        this->arg = arg;
    }
    callback function;
    void* arg;
};

// 任务队列
class TaskQueue
{
public:
    TaskQueue();
    ~TaskQueue();

    // 添加任务
    void addTask(Task& task);
    void addTask(callback func, void* arg);

    // 取出一个任务
    Task takeTask();

    // 获取当前队列中任务个数
    inline size_t taskNumber()
    {
        return m_queue.size();
    }

private:
    pthread_mutex_t m_mutex;    // 互斥锁
    std::queue<Task> m_queue;   // 任务队列
};