/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>
  
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void exe_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);




//HTTP请求，主要是处理两种方式：GET和POST
 
//GET方式头如下：
// Host: 192.168.0.23:47310
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*; q = 0.8
// Accept - Encoding: gzip, deflate, sdch
// Accept - Language : zh - CN, zh; q = 0.8
// Cookie: __guid = 179317988.1576506943281708800.1510107225903.8862; monitor_count = 5

//POST方式如下:
// POST / color1.cgi HTTP / 1.1
// Host: 192.168.0.23 : 47310
// Connection : keep - alive
// Content - Length : 10
// Cache - Control : max - age = 0
// Origin : http ://192.168.0.23:40786
// Upgrade - Insecure - Requests : 1
// User - Agent : Mozilla / 5.0 (Windows NT 6.1; WOW64) AppleWebKit / 537.36 (KHTML, like Gecko) Chrome / 55.0.2883.87 Safari / 537.36
// Content - Type : application / x - www - form - urlencoded
// Accept : text / html, application / xhtml + xml, application / xml; q = 0.9, image / webp, */*;q=0.8
// Referer: http://192.168.0.23:47310/
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-CN,zh;q=0.8
// Cookie: __guid=179317988.1576506943281708800.1510107225903.8862; monitor_count=281
// Form Data(数据包)
// color=gray







/**
 * @name: accept_request()
 * @desc:处理从套接字上监听到的一个HTTP请求，
 *       在这里可以很大一部分地体现服务器处理请求流程。
 * @param {*}
 * @return {*}
 */
 void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI
                       * program */
    char *query_string = NULL;

    //处理get请求的第一行信息
    //所以需要一个逐行读取的函数getline
    //"GET / HTTP/1.1\n"
    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';

    //判断是get还是post
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return ;
    }

    //如果是post，开启cgi标志位
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    //继续索引
    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0)
    {
        //url为需要处理的事情
        query_string = url;
        //寻找非？和非0的字符
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;

        /* GET 方法特点，? 后面为参数*/
        if (*query_string == '?')
        {
            /*开启 cgi */
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    //将url赋予path，由于html在文件htdos中，所以要先拼接字符串再赋值
    sprintf(path, "http_exam%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    //根据路径，如果找不到对应文件
    if (stat(path, &st) == -1) {
        //丢弃headers的信息
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        //回复客户端，找不到
        not_found(client);
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        
        //如果st有读写权限，cgi = 1
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH)    )
            cgi = 1;
        if (!cgi)
            serve_file(client, path);//直接把文件返回给服务器
        else
            exe_cgi(client, path, method, query_string);
    }
    close(client);
}

/**
 * @name: bad_request
 * @desc: post方法，结果没有数据，所以要返回400
 * @param {int} client
 * @return {*}
 */
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}


/**
 * @name: cat
 * @desc:将文件内容传输给客户端
 * @param {int} client
 * @param {FILE} *resource
 * @return {*}
 */
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}


/**
 * @name: cannot_execute
 * @desc: 无法继续操作，返回失败500
 * @param {int} client
 * @return {*}
 */
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**
 * @name: error_die
 * @desc: 报错函数
 * @param {const char} *sc
 * @return {*}
 */
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**
 * @name: exe_cgi
 * @desc:执行cgi脚本，需要设置环境变量，会产生父子进程
 *       采用两个管道实现双工
 * @param {int} client          客户端套接字
 * @param {const char*} path    路径
 * @param {const char*} method  http方法
 * @param {const char*} query_string    执行事件
 * @return {*}
 */
void exe_cgi(int client, const char *path,
        const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];  //输出管道（父子都会有
    int cgi_input[2];   //输入管道（父子都会有
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        ///*把所有的 HTTP header 读取并丢弃*/
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        ///* 对 POST 的 HTTP 请求中找出 content_length */
        //如果是POST请求，就需要得到Content-Length，Content-Length：这个字符串一共长为15位，所以
        //取出头部一句后，将第16位设置结束符，进行比较
        //第16位置为结束
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {           
            ///*利用 \0 进行分隔 */
            buf[15] = '\0';
            /* HTTP 请求的特点*/
            if (strcasecmp(buf, "Content-Length:") == 0)
                 //内存从第17位开始就是长度，将17位开始的所有字符串转成整数就是content_length
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        // 注意到了这里后Post请求头后面的附带信息还没有读出来，要在下面才读取。
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/
    {
        //不会出现这样的情况了，但是还是写上吧
        printf("the method is wrong!");
        unimplemented(client);
        return;
    }

    //记住！先创建pipe，然后fork
    //创建管道output--父子进程都会有写管道
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    //建立input管道---父子进程都会有读管道
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    //fork进程，子进程用于执行CGI
    //父进程用于收数据以及发送子进程处理的回复数据

    if ( (pid = fork()) < 0 ) { //创建子进程 fork()
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);


    // fork后管道都复制了一份，都是一样的

    //       (1)子进程关闭2个无用的端口，避免浪费             
    //       ×<------------------------->1    output
    //       0<-------------------------->×   input 
    
    //       (2)父进程关闭2个无用的端口，避免浪费             
    //       0<-------------------------->×   output
    //       ×<------------------------->1    input


    // 此时父子进程已经可以通信    
    if (pid == 0)  /* 如果是子进程*/
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        
        //子进程输出重定向到output管道的1端
        dup2(cgi_output[1], STDOUT);    //cgi_input[1] 写
        //子进程输入重定向到input管道的0端
        dup2(cgi_input[0], STDIN);  //cgi_input[0] 读取
        //关闭无用管道
        close(cgi_output[0]);
        close(cgi_input[1]);

        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);   //putenv保存到环境变量中，用于给其他进程读取。进程间通讯的一种方式

        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        //替换执行path
        execl(path, NULL);
        exit(0);
    } else {    /* 父进程*/

        //关闭无用管道口
        close(cgi_output[1]);
        close(cgi_input[0]);

        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {
                //得到post请求数据，写到input管道中，供子进程使用
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        //从output管道读到子进程处理后的信息，然后send出去
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);
        
        //完成操作后关闭管道
        close(cgi_output[0]);
        close(cgi_input[1]);
        
        waitpid(pid, &status, 0);
    }
}

/**
 * @name: get_line()
 * @desc:逐行读取函数
 * @param {int} sock 套接字描述符
 * @param {char} *buf   缓冲区
 * @param {int} size    缓冲区大小
 * @return {int} i 读取到字符数
 */
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**
 * @name: headers
 * @desc:先返回一个报头
 * @param {int} client
 * @param {const char} *filename
 * @return {*}
 */
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**
 * @name: not_found
 * @desc:回复客户端，找不到 404
 * @param {int} client
 * @return {*}
 */
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**
 * @name: serve_file
 * @desc:由于没有权限，所以将文件直接返回给服务器
 * @param {int} client
 * @param {const} char
 * @return {*}
 */
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**
 * @name: startup()
 * @desc:初始化 httpd 服务，包括创建套接字，绑定端口，进行监听等
 * @param {u_short} *port , 输入为port端口
 * @return {int} http_ret ， 输出为服务端套接字描述符
 */
int startup(u_short *port)
{
    int http_ret = 0;  //定义服务器的socket描述字，该函数要返回的也是http
    int on = 1;
    struct sockaddr_in name;    //用来绑定服务器的目标地址（ip）+端口（port）

    //创建服务器段的socket， PF_INET代表ipv4, sock_stream 代表tcp协议
    //socket（）函数返回的是 socket描述字，如果失败返回-1
    http_ret = socket(PF_INET, SOCK_STREAM, 0); //Create a new socket
    if (http_ret == -1)
        error_die("socket");
    
    //相当于清零操作
    memset(&name, 0, sizeof(name)); //填充name这块内存为0
    name.sin_family = AF_INET; //AF_INET代表TCP/IP协议族
    name.sin_port = htons(*port); // 端口转为网络字节顺序(大端模式)
    //本机任意可用的ip地址
    name.sin_addr.s_addr = htonl(INADDR_ANY); //host-to-net-long 转换成无符号长整型的网络字节顺序，INADDR_ANY代表本地任意地址
    if ((setsockopt(http_ret, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)  
    {  
        error_die("setsockopt failed");
    }
    if (bind(http_ret, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(http_ret, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);//ntohs: net to host short
    }
    /*
    listen函数的第一个参数即为要监听的socket描述字，第二个参数为相应socket可以排队的最大连接个数。
    */
    if (listen(http_ret, 5) < 0)   //服务器开始监听
        error_die("listen");
    return(http_ret); 
}

/**
 * @name: unimplemented
 * @desc:通知客户端请求的web方法尚未实现。501错误
 * @param {int} client
 * @return {*}
 */
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;   //服务器的socket描述字
    u_short port = 5000;    //服务器监听的端口
    int client_sock = -1;   //客户端的socket描述字
    //sockaddr_in 结构体，是在accept阶段，查看目标地址以及端口的
    struct sockaddr_in client_name; //client_name--套接字地址的结构体，分开包含 目标地址+端口
    socklen_t  client_name_len = sizeof(client_name);   //获取服务端地址的长度
    pthread_t newthread;    //定义线程id号

    ///*在对应端口建立 httpd 服务*/
    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        //accept返回一个客户端和服务端连接的套接字，如果连接失败，则为-1，
        //accept是阻塞的
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");

        //派生一个线程运行
        accept_request(&client_sock); 

        /**
            int pthread_create(
                 pthread_t *restrict tidp,   //新创建的线程ID指向的内存单元。
                 const pthread_attr_t *restrict attr,  //线程属性，默认为NULL
                 void *(*start_rtn)(void *), //新创建的线程从start_rtn函数的地址开始运行
                 void *restrict arg //默认为NULL。若上述函数需要参数，将参数放入结构中并将地址作为arg传入。
                  );
        */
        if (pthread_create(&newthread , 
                            NULL, 
                            (void *)accept_request, 
                            (void *)(intptr_t)client_sock) != 0)
            perror("pthread_create");

    }

    close(server_sock);

    return(0);
}
