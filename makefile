target: app

app: RedisConnect.h RedisCommand.cpp
ifdef WINDIR
	g++ -std=c++11 -pthread -DXG_MINGW -o redis RedisCommand.cpp -lws2_32 -lpsapi -lm
else
	g++ -std=c++11 -pthread -o redis RedisCommand.cpp -lutil -ldl -lm
endif
	
clean:
	@rm redis
