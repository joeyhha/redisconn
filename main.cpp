#include "RedisConnect.h"

int main(int argc, char** argv)
{
	string val;
 
	//初始化连接池
	RedisConnect::Setup("127.0.0.1", 6379, "password");
 
	//从连接池中获取一个连接
	shared_ptr<RedisConnect> redis = RedisConnect::Instance();
 
	//设置一个键值（并解析）
	redis->set("key", "val");
	
	//获取键值内容
	redis->get("key", val);
 
	//执行expire命令设置超时时间
	redis->execute("expire", "key", 60);
 
	//获取超时时间(与ttl(key)方法等价)
	redis->execute("ttl", "key");
 
	//调用getStatus方法获取ttl命令执行结果
	printf("超时时间：%d\n", redis->getStatus());
 
	//执行del命令删除键值
	redis->execute("del", "key");

	//获取分布式锁
	if (redis->lock("lockey"))
	{
		puts("获取分布式锁成功");

		//释放分布式锁
		if (redis->unlock("lockey"))
		{
			puts("释放分布式锁成功");
		}
	}
 
	return 0;
}