# RedisConnect介绍
#### 该项目是使用C++11开发的一个简易版本的Redis客户端，封装了常用的Redis命令，实现了与Redis服务器进行交互的基本功能，可以方便地操作Redis数据库。

# 实现功能
#### 1、使用Epoll建立与Redis服务端的TCP连接，支持设置连接、发送和接收数据的超时时间，以避免长时间的阻塞操作；
#### 2、实现了对常见的 Redis 命令（如GET、SET、DEL等）的解析与执行，同时实现了对命令执行的错误和异常处理；
#### 3、实现并使用连接池来管理Redis连接对象，完成连接复用和自动回收的功能；
#### 4、实现了分布式锁，提供对指定键的加锁和解锁功能。
