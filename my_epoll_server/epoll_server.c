/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-05-24 14:59:37
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-05-26 19:28:37
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include "epoll_server.h"

#define MAXSIZE 2000


void epoll_run(int port)
{
    // 创建一个epoll树的根节点
    int epfd = epoll_create(MAXSIZE);
    if(epfd == -1)
    {
        perror("epoll_create error");
        exit(1);
    }
    
    
    // 添加要监听的节点
    //  先添加监听的lfd
    int lfd = init_listen_fd(port,epfd);

    // 委托内核检测添加到树上的节点
    struct epoll_event all[MAXSIZE];
    while(1)
    {
        int ret = epoll_wait(epfd, all, MAXSIZE, -1);
        if(ret == -1)
        {
            perror("epoll_wait error");
            exit(1);
        }

        // 遍历发生变化的节点
        for(int i=0; i<ret; ++i)
        {
            //只处理读事件，其他事件默认不处理
            struct epoll_event *pev = &all[i];
            if( !(pev->events & EPOLLIN )) 
            {
                //不是读事件
                continue;
            }
            
            //是读事件
            if( pev->data.fd == lfd )//lfd是监听字符
            {
                //接受连接请求
                do_accept(lfd, epfd);
            }
            else
            {
                //通信-即读操作
                do_read( pev->data.fd, epfd );
            }
            
        }
    }
}



//读取数据
void do_read(int cfd, int epfd )
{
    //将浏览器发过来的数据，读取到buffer中 所以要一行一行读取
    char line[1024] = {0};
    //读请求行
    int len = get_line( cfd, line, sizeof(line) );
    if( len == 0 )
    {
        printf("客户端断开连接\n");
        //关闭套接字，将cfd从epoll上删掉
        //采用关闭套接字函数
        disconnect(cfd, epfd);
    }
    else if( len == -1 )
    {
        //recive失败了
        perror("recv error");
        exit(1);
    }
    else
    {
        printf("请求行数据：%s",line);
        //还没读完，继续读取
        while (len)
        {
            char buf[1024] = {0};
            len = get_line(cfd, buf, sizeof(buf));
            printf("==========请求头=========\n");
            printf("---:%s",buf);
        }
    }

    //请求行： get /xxx http/1.1
    //判断是不是get请求
    if( strncasecmp("get", line, 3 )==0 )
    {
        //如果是，则处理http请求,实际上 就是读取/xxx这个路径的东西
        http_request(line, cfd);
        // 关闭套接字, cfd从epoll上del
        disconnect(cfd, epfd);  
    }
}

//断开连接的函数
void disconnect( int cfd, int epfd )
{
    int ret = epoll_ctl( epfd, EPOLL_CTL_DEL, cfd, NULL );
    if( ret == -1 )
    {
        perror(" epoll_ctl_del cfd error ");
        exit(1);
    }
    close(cfd);
}


//http数据请求处理
void http_request(const char* request, int cfd)
{
    //拆分http请求行
    // GET /XXX http/1.1 注意 这里没有\r\n了
    char method[12], path[1024], protocol[12];
    sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);

    printf("method = %s, path = %s, protocol = %s", method, path, protocol);

    //处理path   /xxxx  需要去除/
    char* file = path + 1 ;
    if( strcmp( path,"/") )
    {
        file = "./";
    }


    //获取文件属性
    struct stat st;
    int ret = stat( file, &st );
    if( ret == -1 )
    {
        perror( "stat error" );
        exit(1);
    }
    
    //判断&file是目录还是文件 采用state
    //如果是目录
    if( S_ISDIR(st.st_mode) )
    {
        
        //发送头信息
        send_respond_head( cfd, 200, "OK", "text/html", -1 );
        //发送目录信息
        send_dir(cfd, file );
    }
    else if( S_ISREG(st.st_mode) )//如果是文件
    {
        
        //1、打开文件
        //2、发送给浏览器，要加报头！
        //发送消息报头
        send_respond_head( cfd, 200, "OK", "text/plain", st.st_size );
        //发送文件的内容
        send_file(cfd, file);
    }
}


//发送目录
void send_dir( int cfd, const char* dirname)
{
    //拼一个html的页面
    char buf[4096] = {0};
    sprintf( buf, "<html><head><title>目录名: %s </title></head>", dirname);
    sprintf( buf+strlen(buf), "<body><h1>当前目录：%s</h1><table>", dirname );
#if 0
    //打开目录
    DIR* dir = opendir(dirname);
    if( dir == null )
    {
        perror( " opendir error " );
        exit(1);
    }
    //正常打开目录，接下来读取目录
    struct dirent* ptr = NULL;
    while ((ptr = readdir(dir)) != NULL)
    {
        sprintf( buf,"<tr><td> </td>" );
        char* name = ptr->d_name;
    }
    
    close(dir);
#endif
    //目录项二级指针
    char path[1024] = {0};
    struct dirent** ptr;
    //num表示当前目录有多少个文件
    int num = scandir( dirname, &ptr, NULL, alphasort );
    //遍历
    for( int i =0; i < num; ++i )
    {
        //读取名字
        char* name = ptr[i]->d_name;
        //字符串拼接
        sprintf( path, "%s/%s", dirname, name );
        struct stat st;
        stat( path, &st );

        //如果是文件
        if( S_ISREG(st.st_mode) )
        {
            sprintf( buf+strlen(buf), 
                "<tr><td> <a href = \"%s\">%s</a> </td> <td>%ld</td></tr>" , 
                dirname, dirname, (long)st.st_size );
        }
        //如果是目录
        else if( S_ISDIR(st.st_mode) )
        {
            sprintf( buf+strlen(buf), 
                "<tr><td> <a href = \"%s/\">%s/</a> </td> <td>%ld</td></tr>" , 
                dirname, dirname, (long)st.st_size );
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0, sizeof(buf));
        //字符串拼接
    }

    sprintf( buf+strlen(buf), "</table></body></html>" );
    send( cfd, buf, strlen(buf), 0 );
    printf("dir message send OK!!\n");

}


//发送响应头
void send_respond_head( int cfd, int no, const char* desp, const char* type, long len )
{
    char buf[1024] = {0};
    //状态行
    sprintf( buf, "http/1.1 %d %s\r\n", no, desp );
    send( cfd, buf, strlen(buf), 0 );
    //消息报头
    sprintf( buf, "Content-Type:%s\r\n", type );
    sprintf( buf + strlen(buf), "Content-Length:%ld\r\n", len );
    send( cfd, buf, strlen(buf), 0 );
    //空行
    send( cfd,"\r\n", 2, 0 );
}

//发送文件
void send_file( int cfd, const char* filename )
{
    int fd = open( filename, O_RDONLY );
    if( fd == -1 )
    {
        //show 404
        return ;
    }
    //如果文件被打开了
    char buf[4096] = {0};
    int len = 0;
    while ((len = read( fd, buf, sizeof(buf) )) > 0)
    {
        //发送独处的数据
        send( cfd, buf, len, 0 );
    }
    if( len ==  -1 )
    {
        perror( "read file error" );
        exit(1);
    }
    //没出错，并且读完了
    close(fd);
}

//按照行读取
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                //MSG_PEEK 是看看 缓冲区里有没有数据，有多少数据
                n = recv(sock, &c, 1, MSG_PEEK);
                //如果有数据，读取出来，flag = 0 拷贝读取
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    buf[i] = '\0';

    if( n == -1 )
    {
        i = -1;
    }

    return i;
}

//接受新的连接请求
void do_accept(int lfd, int epfd)
{
    struct sockaddr_in client;
    socklen_t len  = sizeof( client );
    //三个函数
    int cfd = accept( lfd, (struct sockaddr*) &client, &len );
    if( cfd == -1 )
    {
        perror("accept error");
        exit(1);
    }
    //打印客户端信息
    char ip[64]= {0};
    printf("New Client IP: %s, Port: %d ,cfd = %d\n",
        inet_ntop( AF_INET, &client.sin_addr.s_addr, ip, sizeof(ip) ),
        ntohs( client.sin_port ),cfd
    );
    
    //设置cfd为非阻塞
    int flag = fcntl(cfd, F_GETFL);
    flag |=O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    //得到的新节点挂到epoll树上
    struct epoll_event ev;
    //设置边沿非阻塞模式
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = cfd;
    int ret = epoll_ctl( epfd, EPOLL_CTL_ADD, cfd, &ev );
    if(ret == -1)
    {
        perror( 
            " epoll_ctl add cfd error "
         );
        exit(1);
    }
}


//创建lfd的函数，初始化监听套接字
int init_listen_fd( int port, int epfd )
{
    //创建监听套接字
    int lfd = socket( AF_INET, SOCK_STREAM, 0);
    if( lfd == -1 )
    {
        perror("socket error");
        exit(1);
    }

    //lfd绑定本地端口
    struct sockaddr_in serv;
    memset( &serv, 0, sizeof(serv) );
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    
    //先设置端口复用
    int flag = 1;
    setsockopt( lfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag) );
    //再绑定
    int ret = bind(lfd, (struct sockaddr*) &serv, sizeof(serv));
    if( ret == -1 )
    {
        perror("bind error");
        exit(1);
    }

    //监听
    ret = listen(lfd, 64);
    if( ret == -1 )
    {
        perror("listen error");
        exit(1);
    }

    //将lfd挂在epoll根节点epfd上
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    ret = epoll_ctl( epfd, EPOLL_CTL_ADD, lfd, &ev );
    if( ret == -1 )
    {
        perror("epoll_ctl error");
        exit(1);
    }
    
    //返回lfd
    return lfd;
}