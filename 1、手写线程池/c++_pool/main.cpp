/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-07-17 10:34:07
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-07-17 10:36:35
 */
#include <pthread.h>
#include <stdio.h>
#include "ThreadPool.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

void taskFunc(void* arg)
{
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d\n",
        pthread_self(), num);
    sleep(1);
}

int main()
{
    // 创建线程池
    ThreadPool pool(3, 10);
    for (int i = 0; i < 100; ++i)
    {
        int* num = new int(i+100);
        pool.addTask(Task(taskFunc, num));
    }

    sleep(20);

    return 0;
}
