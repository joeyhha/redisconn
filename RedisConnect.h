#ifndef REDIS_CONNECT_H
#define REDIS_CONNECT_H
///////////////////////////////////////////////////////////////
#include "ResPool.h"

#ifdef XG_LINUX

#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/statfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/syscall.h>

#define ioctlsocket ioctl
#define INVALID_SOCKET (SOCKET)(-1)

typedef int SOCKET;

#endif

using namespace std;

class RedisConnect
{
	typedef std::mutex Mutex;
	typedef std::lock_guard<mutex> Locker;

	friend class Command;

public:
	static const int OK = 1;
	static const int FAIL = -1;
	static const int IOERR = -2;
	static const int SYSERR = -3;
	static const int NETERR = -4;
	static const int TIMEOUT = -5;
	static const int DATAERR = -6;
	static const int SYSBUSY = -7;
	static const int PARAMERR = -8;
	static const int NOTFOUND = -9;
	static const int NETCLOSE = -10;
	static const int NETDELAY = -11;
	static const int AUTHFAIL = -12;

public:
	static int POOL_MAXLEN;
	static int SOCKET_TIMEOUT;

public:
	class Socket
	{
	protected:
		SOCKET sock = INVALID_SOCKET;	// 初始sock状态为-1

	public:
		// 看是否超时，没有超时，返回0，超时了返回1
		static bool IsSocketTimeout()
		{
#ifdef XG_LINUX
			return errno == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#else
			return WSAGetLastError() == WSAETIMEDOUT;
#endif
		}
		static void SocketClose(SOCKET sock)
		{
			if (IsSocketClosed(sock)) return;	// 如果套接字已经关闭，则直接返回

#ifdef XG_LINUX
			::close(sock);	// 在Linux系统上关闭套接字
#else
			::closesocket(sock);
#endif
		}
		static bool IsSocketClosed(SOCKET sock)
		{
			return sock == INVALID_SOCKET || sock < 0; 	// 检查套接字是否为无效套接字或小于0，表示套接字已关闭
		}
		// 发送超时时间
		static bool SocketSetSendTimeout(SOCKET sock, int timeout)
		{
#ifdef XG_LINUX
			struct timeval tv;

			tv.tv_sec = timeout / 1000;	// 超时时间转换为秒
			tv.tv_usec = timeout % 1000 * 1000;	// 超时时间的毫秒部分转换为微秒

			// 设置套接字发送超时时间
			return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)(&tv), sizeof(tv)) == 0;
#else
			return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)(&timeout), sizeof(timeout)) == 0;
#endif
		}

		// 设置套接字接收超时时间
		static bool SocketSetRecvTimeout(SOCKET sock, int timeout)
		{
#ifdef XG_LINUX
			struct timeval tv;

			tv.tv_sec = timeout / 1000;
			tv.tv_usec = timeout % 1000 * 1000;
			
			// 在Linux系统上设置套接字接收超时时间
			return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)(&tv), sizeof(tv)) == 0;
#else
			return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)(&timeout), sizeof(timeout)) == 0;
#endif
		}

		// 设置超时连接
		SOCKET SocketConnectTimeout(const char* ip, int port, int timeout)
		{
			u_long mode = 1;
			struct sockaddr_in addr;
			SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);	// 创建一个TCP套接字

			if (IsSocketClosed(sock)) return INVALID_SOCKET;	// 如果套接字创建失败，则返回无效套接字

			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);	// 设置端口号，并进行网络字节序转换
			addr.sin_addr.s_addr = inet_addr(ip);	// 将IP地址字符串转换为网络字节序的32位整数

			ioctlsocket(sock, FIONBIO, &mode); mode = 0;	// 设置套接字为非阻塞模式，以便进行超时连接

			if (::connect(sock, (struct sockaddr*)(&addr), sizeof(addr)) == 0)
			{
				ioctlsocket(sock, FIONBIO, &mode);	// 连接成功后，将套接字设置为阻塞模式

				return sock;
			}

#ifdef XG_LINUX
			struct epoll_event ev;
			struct epoll_event evs;
			int handle = epoll_create(1);	// 创建一个epoll事件句柄

			if (handle < 0)
			{
				SocketClose(sock);	// 关闭套接字
			
				return INVALID_SOCKET;
			}
			
			memset(&ev, 0, sizeof(ev));
			
			ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;	// 监听写事件和错误事件
			
			epoll_ctl(handle, EPOLL_CTL_ADD, sock, &ev);	// 将套接字添加到epoll事件监听中
			
			if (epoll_wait(handle, &evs, 1, timeout) > 0) 	// 等待事件发生
			{
				if (evs.events & EPOLLOUT)	// 如果是写事件
				{
					int res = FAIL;
					socklen_t len = sizeof(res);
			
					getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)(&res), &len); 	// 获取套接字的错误状态
					ioctlsocket(sock, FIONBIO, &mode);	// 将套接字设置为阻塞模式
			
					if (res == 0)
					{
						::close(handle);	// 关闭epoll事件句柄
			
						return sock;
					}
				}
			}
			
			::close(handle);
#else
			struct timeval tv;

			fd_set ws;
			FD_ZERO(&ws);
			FD_SET(sock, &ws);

			tv.tv_sec = timeout / 1000;
			tv.tv_usec = timeout % 1000 * 1000;

			if (select(sock + 1, NULL, &ws, NULL, &tv) > 0)
			{
				int res = ERROR;
				int len = sizeof(res);
			
				getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)(&res), &len);
				ioctlsocket(sock, FIONBIO, &mode);
			
				if (res == 0) return sock;
			}
#endif

			SocketClose(sock); 	// 关闭socket
			
			return INVALID_SOCKET;
		}

	public:
		// 关闭连接
		void close()
		{
			SocketClose(sock);
			sock = INVALID_SOCKET;
		}
		bool isClosed() const
		{
			return IsSocketClosed(sock);
		}
		bool setSendTimeout(int timeout)
		{
			return SocketSetSendTimeout(sock, timeout);
		}
		bool setRecvTimeout(int timeout)
		{
			return SocketSetRecvTimeout(sock, timeout);
		}
		// 超时连接
		bool connect(const string& ip, int port, int timeout)
		{
			close();

			sock = SocketConnectTimeout(ip.c_str(), port, timeout);

			return IsSocketClosed(sock) ? false : true;
		}

	public:
		int write(const void* data, int count)
		{
			const char* str = (const char*)(data);	// 将数据转换为const char*类型

			int num = 0;  // 用于记录每次发送的字节数
    		int times = 0;  // 用于计数发送次数
    		int writed = 0;  // 已发送的字节数


			while (writed < count)
			{
				if ((num = send(sock, str + writed, count - writed, 0)) > 0) 	// 发送数据，返回发送的字节数
				{
					if (num > 8)
					{
						times = 0;	// 如果发送字节数大于8，重置计数器
					}
					else
					{
						if (++times > 100) return TIMEOUT;	// 如果连续发送次数超过100次，则返回超时错误
					}

					writed += num;	// 更新已发送的字节数
				}
				else
				{
					if (IsSocketTimeout())	// 检查是否发生了超时错误
					{
						if (++times > 100) return TIMEOUT;	// 如果连续发送次数超过100次，则返回超时错误

						continue;	// 继续发送剩余数据
					}

					return NETERR;	// 发送错误，返回网络错误
				}
			}

			return writed;	// 返回发送的总字节数
		}
		int read(void* data, int count, bool completed)
		{
			char* str = (char*)(data);	// 将数据转换为char*类型

			if (completed)	// 如果要求读取完整数据
			{
				int num = 0;	// 用于记录每次接收的字节数
				int times = 0;	// 用于计数接收次数
				int readed = 0;	// 已接收的字节数

				while (readed < count)	// 循环接收数据，直到接收完指定数量的字节
				{
					// 接收数据，返回接收的字节数
					if ((num = recv(sock, str + readed, count - readed, 0)) > 0)
					{
						if (num > 8)
						{
							times = 0;
						}
						else
						{
							if (++times > 100) return TIMEOUT;	// 如果连续接收次数超过100次，则返回超时错误
						}

						readed += num;	// 更新已接收的字节数
					}
					else if (num == 0)	// 如果返回值为0，表示连接已关闭
					{
						return NETCLOSE;	// 返回连接关闭错误
					}
					else	// 否则发生了接收错误
					{
						if (IsSocketTimeout())	// 检查是否发生了超时错误
						{	
							// 如果连续接收次数超过100次，则返回超时错误
							if (++times > 100) return TIMEOUT;

							continue;	// 继续接收剩余数据
						}

						return NETERR;	// 接收错误，返回网络错误
					}
				}

				return readed;	// 返回接收的总字节数
			}
			else// 如果不要求读取完整数据
			{
				int val = recv(sock, str, count, 0);	// 接收数据，返回接收的字节数

				if (val > 0) return val;	// 返回接收的字节数

				if (val == 0) return NETCLOSE;	// 返回连接关闭错误

				if (IsSocketTimeout()) return 0;	// 如果发生超时错误，则返回0表示没有数据可读

				return NETERR;	// 接收错误，返回网络错误
			}
		}
	};

	class Command
	{
		friend RedisConnect;

	protected:
		int status;	// 命令的状态码
		string msg;	// 命令的状态信息
		vector<string> res;	// 命令的结果列表
		vector<string> vec;	// 命令的参数列表

	protected:
		int parse(const char* msg, int len)
		{	
			// 如果消息字符串以$开头
			if (*msg == '$')
			{
				const char* end = parseNode(msg, len);	// 解析节点信息

				if (end == NULL) return DATAERR;	// 节点解析错误，返回数据错误

				switch (end - msg)
				{
				case 0: return TIMEOUT;	// 超时错误
				case -1: return NOTFOUND;	// 未找到
				}

				return OK;	// 解析成功
			}

			const char* str = msg + 1;	// 跳过命令标识符
			const char* end = strstr(str, "\r\n");	// 查找命令结束标志

			if (end == NULL) return TIMEOUT;	// 超时错误

			if (*msg == '+' || *msg == '-' || *msg == ':')
			{
				this->status = OK;
				this->msg = string(str, end);	// 解析状态消息

				if (*msg == '+') return OK;	// 返回成功状态
				if (*msg == '-') return FAIL;	// 返回失败状态

				this->status = atoi(str);	// 解析数字状态

				return OK;	// 解析成功
			}

			if (*msg == '*')
			{
				int cnt = atoi(str);	// 解析参数个数
				const char* tail = msg + len;	// 命令尾部位置

				vec.clear();	// 清空参数列表
				str = end + 2;	// 跳过参数个数和换行符

				while (cnt > 0)
				{
					if (*str == '*') return parse(str, tail - str);	// 递归解析子命令

					end = parseNode(str, tail - str);	// 解析节点信息

					if (end == NULL) return DATAERR;	// 节点解析错误
					if (end == str) return TIMEOUT;	// 超时错误

					str = end;
					cnt--;
				}

				return res.size();	// 返回结果列表的大小
			}

			return DATAERR;
		}
		const char* parseNode(const char* msg, int len)
		{
			const char* str = msg + 1;	// 跳过节点标识符
			const char* end = strstr(str, "\r\n");	// 查找节点结束标志

			if (end == NULL) return msg;	// 节点未完整接收，返回原始数据指针

			int sz = atoi(str);	// 解析节点大小

			if (sz < 0) return msg + sz;	// 负数表示引用之前已解析的节点

			str = end + 2;	// 跳过节点大小和换行符
			end = str + sz + 2;	// 节点尾部位置

			if (msg + len < end) return msg;	// 节点未完整接收，返回原始数据指针

			res.push_back(string(str, str + sz));	// 添加节点值到结果列表

			return end;	// 返回节点结束位置
		}

	public:
		Command()
		{
			this->status = 0;
		}
		Command(const string& cmd)
		{
			vec.push_back(cmd);
			this->status = 0;
		}
		void add(const char* val)
		{
			vec.push_back(val);
		}
		void add(const string& val)
		{
			vec.push_back(val);
		}
		template<class DATA_TYPE> 
		void add(DATA_TYPE val)
		{
			add(to_string(val));
		}
		template<class DATA_TYPE, class ...ARGS> 
		void add(DATA_TYPE val, ARGS ...args)
		{
			add(val);
			add(args...);
		}

	public:
		string toString() const
		{
			ostringstream out; // 字符串类型输出流对象

			out << "*" << vec.size() << "\r\n";	// 添加命令参数个数到字符串流

			for (const string& item : vec)
			{
				out << "$" << item.length() << "\r\n" << item << "\r\n";	// 添加参数长度和参数值到字符串流
			}

			return out.str();	// 将字符串流转换为字符串并返回
		}
		string get(int idx) const
		{
			return res.at(idx);
		}
		const vector<string>& getDataList() const
		{
			return res;
		}

		// 正常处理了结果，返回1
		int getResult(RedisConnect* redis, int timeout)
		{
			auto doWork = [&]() {
				string msg = toString();	// 将命令转换为字符串
				Socket& sock = redis->sock;

				if (sock.write(msg.c_str(), msg.length()) < 0) return NETERR;	// 将命令字符串写入套接字进行发送

				int len = 0;
				int delay = 0;
				int readed = 0;
				char* dest = redis->buffer;
				const int maxsz = redis->memsz;

				while (readed < maxsz)
				{	
					// 从套接字读取响应数据
					if ((len = sock.read(dest + readed, maxsz - readed, false)) < 0) return len;

					if (len == 0)
					{
						delay += SOCKET_TIMEOUT;	// 延迟时间累加

						if (delay > timeout) return TIMEOUT;	// 超时错误
					}
					else
					{
						dest[readed += len] = 0;	// 添加字符串结束符

						if ((len = parse(dest, readed)) == TIMEOUT)	// 解析响应数据
						{
							delay = 0;	// 重置延迟时间
						}
						else
						{
							return len;	// 返回解析结果
						}
					}
				}

				return PARAMERR;	// 参数错误
			};

			status = 0;
			msg.clear();

			redis->code = doWork();	// 执行工作函数获取结果

			if (redis->code < 0 && msg.empty())	// 如果返回错误码并且消息为空
			{
				switch (redis->code)
				{
				case SYSERR:
					msg = "system error";
					break;
				case NETERR:
					msg = "network error";
					break;
				case DATAERR:
					msg = "protocol error";
					break;
				case TIMEOUT:
					msg = "response timeout";
					break;
				case NOTFOUND:
					msg = "element not found";
					break;
				default:
					msg = "unknown error";
					break;
				}
			}

			redis->status = status;	// 更新连接状态
			redis->msg = msg;	// 更新消息

			return redis->code;	// 返回结果码
		}
	};

protected:
	int code = 0;
	int port = 0;
	int memsz = 0;
	int status = 0;
	int timeout = 0;
	char* buffer = NULL;

	string msg;
	string host;
	Socket sock;
	string passwd;

public:
	~RedisConnect()
	{
		close();
	}

public:
	int getStatus() const
	{
		return status;
	}
	int getErrorCode() const
	{
		if (sock.isClosed()) return FAIL;

		return code < 0 ? code : 0;
	}
	string getErrorString() const
	{
		return msg;
	}

public:
	void close()
	{
		if (buffer)
		{
			delete[] buffer;
			buffer = NULL;
		}

		sock.close();
	}

	// 重连
	bool reconnect()
	{
		if (host.empty()) return false;

		return connect(host, port, timeout, memsz) && auth(passwd) > 0;
	}
	int execute(Command& cmd)
	{
		return cmd.getResult(this, timeout);
	}
	template<class DATA_TYPE, class ...ARGS>
	int execute(DATA_TYPE val, ARGS ...args)
	{
		Command cmd;

		cmd.add(val, args...);

		return cmd.getResult(this, timeout);
	}
	template<class DATA_TYPE, class ...ARGS>
	int execute(vector<string>& vec, DATA_TYPE val, ARGS ...args)
	{
		Command cmd;

		cmd.add(val, args...);

		cmd.getResult(this, timeout);

		if (code > 0) std::swap(vec, cmd.res);

		return code;
	}
	// 连接到指定的主机和端口，返回值为是否成功建立连接
	bool connect(const string& host, int port, int timeout = 3000, int memsz = 2 * 1024 * 1024)
	{
		close();

		if (sock.connect(host, port, timeout))
		{
			sock.setSendTimeout(SOCKET_TIMEOUT);
			sock.setRecvTimeout(SOCKET_TIMEOUT);

			this->host = host;
			this->port = port;
			this->memsz = memsz;
			this->timeout = timeout;
			this->buffer = new char[memsz + 1];
		}

		return buffer ? true : false;
	}

public:
	int ping()
	{
		return execute("ping");
	}
	int del(const string& key)
	{
		return execute("del", key);
	}
	int ttl(const string& key)
	{
		return execute("ttl", key) == OK ? status : code;
	}
	int hlen(const string& key)
	{
		return execute("hlen", key) == OK ? status : code;
	}
	int auth(const string& passwd)
	{
		this->passwd = passwd;

		if (passwd.empty()) return OK;

		return execute("auth", passwd);
	}
	int get(const string& key, string& val)
	{
		vector<string> vec;

		if (execute(vec, "get", key) <= 0) return code;

		val = vec[0];

		return code;
	}
	int decr(const string& key, int val = 1)
	{
		return execute("decrby", key, val);
	}
	int incr(const string& key, int val = 1)
	{
		return execute("incrby", key, val);
	}
	int expire(const string& key, int timeout)
	{
		return execute("expire", key, timeout);
	}
	int keys(vector<string>& vec, const string& key)
	{
		return execute(vec, "keys", key);
	}
	int hdel(const string& key, const string& filed)
	{
		return execute("hdel", key, filed);
	}
	int hget(const string& key, const string& filed, string& val)
	{
		vector<string> vec;

		if (execute(vec, "hget", key, filed) <= 0) return code;

		val = vec[0];

		return code;
	}
	int set(const string& key, const string& val, int timeout = 0)
	{
		return timeout > 0 ? execute("setex", key, timeout, val) : execute("set", key, val);
	}
	int hset(const string& key, const string& filed, const string& val)
	{
		return execute("hset", key, filed, val);
	}

public:
	int pop(const string& key, string& val)
	{
		return lpop(key, val);
	}
	int lpop(const string& key, string& val)
	{
		vector<string> vec;

		if (execute(vec, "lpop", key) <= 0) return code;

		val = vec[0];

		return code;
	}
	int rpop(const string& key, string& val)
	{
		vector<string> vec;

		if (execute(vec, "rpop", key) <= 0) return code;

		val = vec[0];

		return code;
	}
	int push(const string& key, const string& val)
	{
		return rpush(key, val);
	}
	int lpush(const string& key, const string& val)
	{
		return execute("lpush", key, val);
	}
	int rpush(const string& key, const string& val)
	{
		return execute("rpush", key, val);
	}
	int range(vector<string>& vec, const string& key, int start, int end)
	{
		return execute(vec, "lrange", key, start, end);
	}
	int lrange(vector<string>& vec, const string& key, int start, int end)
	{
		return execute(vec, "lrange", key, start, end);
	}

public:
	int zrem(const string& key, const string& filed)
	{
		return execute("zrem", key, filed);
	}
	int zadd(const string& key, const string& filed, int score)
	{
		return execute("zadd", key, score, filed);
	}
	int zrange(vector<string>& vec, const string& key, int start, int end, bool withscore = false)
	{
		return withscore ? execute(vec, "zrange", key, start, end, "withscores") : execute(vec, "zrange", key, start, end);
	}


public:
	template<class ...ARGS>
	int eval(const string& lua)
	{
		vector<string> vec;

		return eval(lua, vec);
	}
	template<class ...ARGS>
	int eval(const string& lua, const string& key, ARGS ...args)
	{
		vector<string> vec;
	
		vec.push_back(key);
	
		return eval(lua, vec, args...);
	}
	template<class ...ARGS>
	int eval(const string& lua, const vector<string>& keys, ARGS ...args)
	{
		vector<string> vec;

		return eval(vec, lua, keys, args...);
	}
	template<class ...ARGS>
	int eval(vector<string>& vec, const string& lua, const vector<string>& keys, ARGS ...args)
	{
		int len = 0;
		Command cmd("eval");

		cmd.add(lua);
		cmd.add(len = keys.size());

		if (len-- > 0)
		{
			for (int i = 0; i < len; i++) cmd.add(keys[i]);

			cmd.add(keys.back(), args...);
		}

		cmd.getResult(this, timeout);
	
		if (code > 0) std::swap(vec, cmd.res);

		return code;
	}

	string get(const string& key)
	{
		string res;

		get(key, res);

		return res;
	}
	string hget(const string& key, const string& filed)
	{
		string res;

		hget(key, filed, res);

		return res;
	}

	const char* getLockId()
	{
		// 创建一个线程局部变量，用于存储锁的标识符
		thread_local char id[0xFF] = {0};

		// 定义一个内部函数 GetHost，用于获取主机名
		auto GetHost = [](){
			char hostname[0xFF];

			/// 获取主机名
			if (gethostname(hostname, sizeof(hostname)) < 0) return "unknow host";

			// 根据主机名获取 IP 地址
			struct hostent* data = gethostbyname(hostname);

			// 返回字符串形式的IP地址
			return (const char*)inet_ntoa(*(struct in_addr*)(data->h_addr_list[0]));
		};

		// 如果锁的标识符尚未被初始化
		if (*id == 0)
		{
#ifdef XG_LINUX
			// 在 Linux 平台下，使用主机名、进程 ID 和线程 ID 生成锁的标识符字符串
			snprintf(id, sizeof(id) - 1, "%s:%ld:%ld", GetHost(), (long)getpid(), (long)syscall(SYS_gettid));
#else
			snprintf(id, sizeof(id) - 1, "%s:%ld:%ld", GetHost(), (long)GetCurrentProcessId(), (long)GetCurrentThreadId());
#endif
		}

		// 返回锁的标识符字符串
		return id;
	}
	bool unlock(const string& key)
	{
		const char* lua = "if redis.call('get',KEYS[1])==ARGV[1] then return redis.call('del',KEYS[1]) else return 0 end";

		return eval(lua, key, getLockId()) > 0 && status == OK;
	}
	bool lock(const string& key, int timeout = 30)
	{
		int delay = timeout * 1000;

		for (int i = 0; i < delay; i += 10)
		{
			if (execute("set", key, getLockId(), "nx", "ex", timeout) >= 0) return true;

			Sleep(10);
		}

		return false;
	}

protected:
	virtual shared_ptr<RedisConnect> grasp() const
	{
		static ResPool<RedisConnect> pool([&]() {
			shared_ptr<RedisConnect> redis = make_shared<RedisConnect>();
			// 如果创建好了redis对象 且 与服务器成功建立连接
			if (redis && redis->connect(host, port, timeout, memsz))
			{	
				// 成功进行身份验证，则返回redis对象
				if (redis->auth(passwd)) return redis;
			}
			// 否则返回NULL
			return redis = NULL;
		}, POOL_MAXLEN);

		// 从资源池中获取可用的RedisConnect对象
		shared_ptr<RedisConnect> redis = pool.get();

		// 若对象存在 且 存在错误
		if (redis && redis->getErrorCode())
		{
			pool.disable(redis); // 将对象置为不可用状态

			return grasp();// 递归调用 grasp() 函数重新获取对象
		}

		return redis;
	}

public:
	static bool CanUse()
	{
		return GetTemplate()->port > 0;
	}
	static RedisConnect* GetTemplate()
	{
		static RedisConnect redis;
		return &redis;
	}
	static void SetMaxConnCount(int maxlen)
	{
		if (maxlen > 0) POOL_MAXLEN = maxlen;
	}
	static shared_ptr<RedisConnect> Instance()
	{	
		// GetTemplate()的返回值是一个RedisConnect类型的指针，所以可以用->调用grasp()
		return GetTemplate()->grasp();
	}
	static void Setup(const string& host, int port, const string& passwd = "", int timeout = 3000, int memsz = 2 * 1024 * 1024)
	{
#ifdef XG_LINUX
		signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
#else
		WSADATA data; WSAStartup(MAKEWORD(2, 2), &data);
#endif
		RedisConnect* redis = GetTemplate();

		redis->host = host;
		redis->port = port;
		redis->memsz = memsz;
		redis->passwd = passwd;
		redis->timeout = timeout;
	}
};

int RedisConnect::POOL_MAXLEN = 8;
int RedisConnect::SOCKET_TIMEOUT = 10;
	
///////////////////////////////////////////////////////////////
#endif