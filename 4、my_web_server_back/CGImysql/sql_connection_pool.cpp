
/**数据库连接功能主要包括：
 * 		初始化: connection_pool::connection_pool
 * 		获取连接: MYSQL* connection_pool::GetConnection()
 * 		释放连接: bool connection_pool::ReleaseConnection(MYSQL * con)
 * 		销毁连接池: void connection_pool::DestroyPool()
 * */

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
#include<stdio.h>
#include<string>
#include<string.h>
#include<stdlib.h>
#include<list>
#include<pthread.h>
#include<iostream>
#include "sql_connection_pool.h"

using namespace std;





//单例模式，唯一的一个静态实例 connPool
connection_pool* connection_pool::connPool = NULL;

//构造初始化
//使用信号量实现多线程争夺连接的同步机制，
//这里将信号量初始化为数据库的连接总数。
connection_pool::connection_pool(string url,		//url地址
								 string User,		//用户名
								 string PassWord,	//密码
								 string DBName,		//数据库名称database
								 int Port,			//端口：3306
								 unsigned int MaxConn)//最大连接数
{
	this->url = url;
	this->Port = Port;
	this->User = User;
	this->PassWord = PassWord;
	this->DatabaseName=DBName;

	pthread_mutex_lock(&lock);
	//创建MaxConn条数据库连接
	for(int i = 0; i < MaxConn; i++)
	{
		MYSQL *con=NULL;
		//1、初始化MYSQL，初始化连接
		con = mysql_init(con);
		
		if(con == NULL)
		{
			cout<<"Error:"<<mysql_error(con);
			exit(1);
		}
		//2、建立一个到mysql数据库的连接
		con = mysql_real_connect(con,url.c_str(),User.c_str(),PassWord.c_str(),DBName.c_str(),Port,NULL,0);

		if(con == NULL)
		{
			cout<<"Error: "<<mysql_error(con);
			exit(1);
		}
		//更新连接池和空闲连接数量
		//连接成功就保存起来
		connList.push_back(con);
		//当前空闲的连接数
		++FreeConn;
	}

	this->MaxConn = MaxConn;
	this->CurConn = 0;
	pthread_mutex_unlock(&lock);
}

//获得实例，只会有一个
connection_pool* connection_pool::GetInstance(string url,string User,string PassWord,string DBName,int Port,unsigned int MaxConn)
{
	//先判断是否为空，若为空则创建，否则直接返回现有
	if(connPool == NULL)
	{
		connPool = new connection_pool(url,User,PassWord,DBName,Port,MaxConn);
	}

	return connPool;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* connection_pool::GetConnection()
{
	MYSQL * con = NULL;
	pthread_mutex_lock(&lock);
	//reserve.wait();
	//连接池大小大于0
	if(connList.size() > 0)
	{
		//取出来一个嘛
		con = connList.front();
		connList.pop_front();
		
		//更新大小
		//TODO：其实没用到这两个变量，可以加东西啊
		--FreeConn;
		++CurConn;
		
		pthread_mutex_unlock(&lock);
		return con;
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL * con)
{
	pthread_mutex_lock(&lock);
	//有连接，关掉就ok
	if(con != NULL)
	{
		connList.push_back(con);
		++FreeConn;
		--CurConn;

		pthread_mutex_unlock(&lock);
		//reserve.post();
		return true;
	}
	pthread_mutex_unlock(&lock);
	return false;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
	pthread_mutex_lock(&lock);
	if(connList.size() > 0)
	{
		////通过迭代器遍历，关闭数据库连接
		list<MYSQL *>::iterator it;
		for(it = connList.begin(); it != connList.end(); ++it)
		{
			//全部关闭
			MYSQL * con = *it;
			mysql_close(con);
		}
		CurConn = 0;
		FreeConn = 0;
		//列表清空
		connList.clear();
		pthread_mutex_unlock(&lock);
	}
	pthread_mutex_unlock(&lock);
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}


//析构即销毁了
connection_pool::~connection_pool()
{
	DestroyPool();
}
