/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-07-13 17:28:38
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-07-30 21:44:28
 */


/**注册和登录：采用cgi方式
    > * HTTP请求采用POST方式
    > * 登录用户名和密码校验
    > * 用户注册及多线程注册安全
 * 
 * 本项目中，使用数据库连接池实现服务器访问数据库的功能，使用POST请求完成注册和登录的校验工作。
 * 注册登录功能有两种实现方案：
 *      其一是使用同步实现；
 *          (1)载入数据库表; 结合代码将数据库中的数据载入到服务器中，不安全呢！
 *          (2)提取用户名和密码; 结合代码对报文进行解析，提取用户名和密码。
 *          (3)注册登录流程; 结合代码对描述服务器进行注册和登录校验的流程。
 *          (4)页面跳转; 结合代码对页面跳转机制进行详解。
 *      其二是使用CGI，通过execl多进程异步实现。
 * 由于进程间不能传递指针，CGI方案又可以分为使用连接池与不使用连接池两种情况。
 * 
 * 
*/
/**
 * c++使用mysql库的流程：
 * 1、使用mysql_init()初始化连接
 * 2、使用mysql_real_connect()建立一个到mysql数据库的连接
 * 3、使用mysql_query()执行查询语句
 * 4、使用result = mysql_store_result(mysql)获取结果集
 * 5、使用mysql_num_fields(result)获取查询的列数；
 * 	     mysql_num_rows(result)获取结果集的行数
 * 6、通过mysql_fetch_row(result)不断获取下一行，然后循环输出
 * 7、使用mysql_free_result(result)释放结果集所占内存
 * 8、使用mysql_close(conn)关闭连接
 * 
*/

#include<mysql/mysql.h>
#include<iostream>
#include<string>
#include<string.h>
#include<cstdio>
#include"sql_connection_pool.h"
#include<map>
using namespace std;


int main(int argc,char *argv[])
{
    //用户名和密码,有点不安全嗷
    //将数据库中的用户名和密码载入到服务器的map中来，
    //map中的key为用户名，value为密码。
    map<string,string> users;

    pthread_mutex_t lock;
    pthread_mutex_init(&lock, NULL);

    //初始化数据库连接池，连接池为静态大小
    //通过主机地址和登录账号，密码进入服务器数据库，选择datebase
    connection_pool *connPool=connection_pool::GetInstance("localhost","root","root","qgydb",3306,5);
    
    //在连接池中取一个连接
    MYSQL *mysql=connPool->GetConnection();
    //在user表中检索username，passwd数据，浏览器端输入
    //执行语句
    if(mysql_query(mysql,"SELECT username,passwd FROM user"))
    {
        printf("INSERT error:%s\n",mysql_error(mysql));
        return -1;
    }
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    
    //返回结果集中的列数
    //TODO：整点东西嗷
    int num_fields = mysql_num_fields(result);
    
    //返回所有字段结构的数组
    MYSQL_FIELD *fields=mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row=mysql_fetch_row(result))
    {
        //TODO：加点刺激的
        string temp1(row[0]);//账号
        string temp2(row[1]);//密码
        users[temp1]=temp2;
    }

    string name(argv[1]);
    const char *namep = name.c_str();
    string passwd(argv[2]);
    const char *passwdp = passwd.c_str();
    char flag = *argv[0];//2 登录校验， 3 注册校验
	
    //如果是注册，先检测数据库中是否有重名的
    //没有重名的，进行增加数据
    char *sql_insert = (char*)malloc(sizeof(char)*200);
    strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
    strcat(sql_insert, "'");
    strcat(sql_insert, namep);
    strcat(sql_insert, "', '");
    strcat(sql_insert, passwdp);
    strcat(sql_insert, "')");
 
    if(flag == '3'){//注册
	if(users.find(name)==users.end()){

        //向数据库中插入数据时，需要通过锁来同步数据
	    pthread_mutex_lock(&lock);
	    int res = mysql_query(mysql,sql_insert);
	    pthread_mutex_unlock(&lock);

        //校验成功，跳转登录页面
	    if(!res)
	    	printf("1\n");
        //校验失败，跳转注册失败页面
	    else
		printf("0\n");
	}
	else
	    printf("0\n");
    }
    //如果是登录，直接判断
    //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
    else if(flag == '2'){
	    if(users.find(name)!=users.end()&&users[name]==passwd)
		printf("1\n");
	    else
		printf("0\n");
    }
    else
	printf("0\n");
    //释放结果集使用的内存
    mysql_free_result(result);

    connPool->DestroyPool();
}

