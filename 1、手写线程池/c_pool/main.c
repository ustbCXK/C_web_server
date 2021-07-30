/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-07-28 15:59:05
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-07-30 21:19:11
 */

#include <stdio.h>
#include "threadpool.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

void taskFunc(void* arg)
{
    int num = *(int*)arg;
    printf("Now is thread %ld",pthread_self());
    printf("thread number is :", num);
    sleep(2);
}

int main()
{
    // 创建线程池
    //初始
    ThreadPool* pool = threadPoolCreate(4, //初始化线程数量
                        10,                 //最大线程数量
                        100);               //任务对立的大小
    //创建任务
    for (int i = 0; i < 100; ++i)
    {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        threadPoolAdd(pool, taskFunc, num);
    }

    sleep(30);

    //销毁线程池
    threadPoolDestroy(pool);
    return 0;
}
