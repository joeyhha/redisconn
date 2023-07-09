#include "RedisConnect.h"

#define ColorPrint(__COLOR__, __FMT__, ...)		\
SetConsoleTextColor(__COLOR__);					\
printf(__FMT__, __VA_ARGS__);					\
SetConsoleTextColor(eWHITE);					\

bool CheckCommand(const char* fmt, ...)
{
	va_list args;
	char buffer[64 * 1024] = {0};

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
	va_end(args);

	printf("%s", buffer);

	while (true)
	{
		int n = getch();

		if (n == 'Y' || n == 'y')
		{
			SetConsoleTextColor(eGREEN);
			printf(" (YES)\n");
			SetConsoleTextColor(eWHITE);

			return true;
		}
		else if (n == 'N' || n == 'n')
		{
			SetConsoleTextColor(eRED);
			printf(" (NO)\n");
			SetConsoleTextColor(eWHITE);

			return false;
		}
	}

	return false;
}

int main(int argc, char** argv)
{
	auto GetCmdParam = [&](int idx){
		return idx < argc ? argv[idx] : NULL;
	};

	string val;
	RedisConnect redis;
	const char* ptr = NULL;
	const char* cmd = GetCmdParam(1);
	const char* key = GetCmdParam(2);
	const char* field = GetCmdParam(3);

	int port = 6379;
	const char* host = getenv("REDIS_HOST");
	const char* passwd = getenv("REDIS_PASSWORD");

	if (host)
	{
		// 如果主机名中有 ：，代表主机名包含端口号信息
		if (ptr = strchr(host, ':'))
		{
			static string shost(host, ptr);// 提取主机名部分
			port = atoi(ptr + 1);	// 提取端口号并转换为整数
			host = shost.c_str();	// 更新主机名为提取的部分
		}
	}

	// 主机名为空 或 空字符串，设置主机名为本机
	if (host == NULL || *host == 0) host = "127.0.0.1";

	// 连接redis服务器
	if (redis.connect(host, port))
	{
		if (passwd && *passwd)
		{
			if (redis.auth(passwd) < 0)
			{
				ColorPrint(eRED, "REDIS[%s][%d]验证失败\n", host, port);

				return -1;
			}
		}

		// 输入的指令为空
		if (cmd == NULL)
		{
			ColorPrint(eRED, "%s\n", "请输入要执行的命令");

			return -1;
		}

		int res = 0;
		string tmp = cmd;

		// 将命令转换为大写字母
		std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::toupper);

		if (tmp == "DELS" && key && *key) // 如果命令为 "DELS"，且提供了键名
		{
			vector<string> vec;

			if (redis.keys(vec, key) > 0)
			{
				if (CheckCommand("确认要删除键值[%s]？", key))
				{
					ColorPrint(eWHITE, "%s\n", "--------------------------------------");

					for (const string& item : vec)
					{
						if (redis.del(item))
						{
							ColorPrint(eGREEN, "删除键值[%s]成功\n", item.c_str());
						}
						else
						{
							ColorPrint(eRED, "删除键值[%s]失败\n", item.c_str());
						}
					}

					ColorPrint(eWHITE, "%s\n\n", "--------------------------------------");
				}
			}
			// key不存在
			else
			{
				ColorPrint(eRED, "删除键值[%s]失败\n", key);
			}
		}
		else
		{
			int idx = 1;
			RedisConnect::Command request;
			
			// 循环添加命令的参数到 request 中
			while (true)
			{
				const char* data = GetCmdParam(idx++);

				if (data == NULL) break;

				request.add(data);
			}

			// 执行命令
			if ((res = redis.execute(request)) >= 0)
			{
				ColorPrint(eWHITE, "执行命令[%s]成功[%d][%d]\n", cmd, res, redis.getStatus());

				const vector<string>& vec = request.getDataList();

				if (vec.size() > 0)
				{
					ColorPrint(eWHITE, "%s\n", "--------------------------------------");

					for (const string& msg : vec)
					{
						ColorPrint(eGREEN, "%s\n", msg.c_str());
					}

					ColorPrint(eWHITE, "%s\n", "--------------------------------------");
					ColorPrint(eWHITE, "共返回%ld条记录\n\n", vec.size());
				}
			}
			else
			{
				ColorPrint(eRED, "执行命令[%s]失败[%d][%s]\n", cmd, res, redis.getErrorString().c_str());
			}
		}
	}
	else
	{
		ColorPrint(eRED, "REDIS[%s][%d]连接失败\n", host, port);
	}

	return 0;
}
