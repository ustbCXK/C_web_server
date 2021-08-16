/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-07-13 17:28:38
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-07-30 21:44:46
 */

/**
 * 数据库连接池
	> * 单例模式，保证唯一
	> * list实现连接池
	> * 连接池为静态大小
	> * 互斥锁实现线程安全
 * 
 * 	本项目中数据库模块分为两部分：
 * 	(1)数据库连接池的定义，
 * 	(2)利用连接池访问数据库，完成登录、注册的校验功能。
 * 具体的，工作线程从数据库连接池取得一个连接，访问数据库中的数据，
 * 访问完毕后将连接还给连接池。
 * 
 ********************************************************
 * 
 * 连接池内容：
 * 	连接池中的资源为一组数据库连接，由程序动态地对池中的连接进行使用，释放。
 * 数据库连接：
 * 	一般访问数据库，先系统创建数据库连接，完成数据库操作，然后系统断开数据库连接。
 * 采用连接池的原因：
 * 	从一般流程中可以看出，如果系统需要频繁访问数据库，则需要频繁创建和断开
 * 数据库连接，而创建数据库连接耗时、不安全呢！
 * 
 ********************************************************
 * 
*/


#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>
#include "../lock/locker.h"

using namespace std;


/**数据库连接功能主要包括：
 * 		初始化，
 * 		获取连接、
 * 		释放连接，
 * 		销毁连接池。
 * */
class connection_pool
{
	public:
		MYSQL * GetConnection();		//获取数据库连接
		bool ReleaseConnection(MYSQL* conn);	//释放连接
		void DestroyPool();			//销毁所有连接

		//单例模式获取一个连接,谨记要用局部静态变量
		static connection_pool * GetInstance(string url,string User,string PassWord,string DataName,int Port,unsigned int MaxConn);
		int GetFreeConn();

		~connection_pool();

	private:
		unsigned int MaxConn;	//最大连接数
		unsigned int CurConn;	//当前已使用的连接数
		unsigned int FreeConn;	//当前空闲的连接数

	private:
		pthread_mutex_t lock;	//互斥锁
		list<MYSQL *> connList;	//连接池
		connection_pool * conn;
		MYSQL *Con;
		connection_pool(string url,string User,string PassWord,string DataBaseName,int Port,unsigned int MaxConn);//构造方法
		static connection_pool *connPool;//唯一的一个静态实例
		//sem reserve;
	private:
		string url;		//主机地址
		string Port;		//数据库端口号
		string User;		//登陆数据库用户名
		string PassWord;	//登陆数据库密码
		string DatabaseName;	//使用数据库名
};

#endif
