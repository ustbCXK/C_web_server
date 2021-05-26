/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-05-24 15:01:57
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-05-24 15:06:57
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, const char* argv[])
{
    if( argc < 3 )
    {
        //port为端口，path为资源所在目录
        printf("eg: ./a.out port path\n");
        exit(1);
    }
    int port = atoi( argv[1] );
    //修改进程的工作目录，方便后续操作
    int ret = chdir(argv[2]);
    if( ret == -1 )
    {
        printf("chdir error");
        exit(1);
    }
    
    //启动epoll模型
    return 0;
}