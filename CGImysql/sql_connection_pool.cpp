#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)//url="localhost"，用户名，密码，数据库名，端口3306，最大连接数，是否关闭日志
{
	m_url = url;//“localhost”
	m_Port = Port;//9006
	m_User = User;//root
	m_PassWord = PassWord;//123
	m_DatabaseName = DBName;//wang
	m_close_log = close_log;//0不关闭

	for (int i = 0; i < MaxConn; i++)//根据数据库连接池循环
	{
		MYSQL *con = NULL;//mysql对象
		con = mysql_init(con);//初始化

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);//连接数据库

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);//放到连接池中
		++m_FreeConn;//空闲的连接数量
	}

	reserve = sem(m_FreeConn);//根据空闲的连接数创建相应的信号量

	m_MaxConn = m_FreeConn;//根据创建的连接数重新赋值最大连接数，防止有创建失败的
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;//创建一个mysql对象

	if (0 == connList.size())//如果连接队列的数量为0，则说明无连接可用
		return NULL;

	reserve.wait();//信号量减1，因为即将占用一个连接
	
	lock.lock();//对连接池加锁

	con = connList.front();//取出连接池队列最前面的mysql对象
	connList.pop_front();

	--m_FreeConn;//可用的连接数减1
	++m_CurConn;//正在使用的连接数加1

	lock.unlock();//解锁
	return con;//返回这取出的mysql对象
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){//传入的是一个空的mysql对象和一个连接池
	*SQL = connPool->GetConnection();//从数据库连接池中获得这个连接对象
	
	conRAII = *SQL;//把取出的mysql对象赋给成员变量connRAII
	poolRAII = connPool;//连接池给成员变量poolRAII
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);//析构
}