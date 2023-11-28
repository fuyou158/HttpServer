#ifndef SERVER_H
#define SERVER_H

/*服务器端要处理的业务逻辑*/ 

// 初始化监听的文件描述符
int initListenFd(unsigned short port);

// 启动epoll模型
int epollRun(unsigned short port);

// 和客户端建立新连接
int acceptConn(int lfd, int epfd);

// 接收客户端的HTTP请求消息
int recvHttpRequest(int cfd, int epfd);

// 解析请求行
int parseRequestLine(int cfd, const char* reqLine);

// 发送头信息（状态行+消息报头+空行）
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length);

// 读文件内容，并发送
int sendFile(int cfd, const char* fileName);
    // 在发送内容之前，应该有状态行+消息报头+空行+文件内容
    // 这四部分内容不需要组织好再发送，因为底层默认使用的是TCP协议，面向连接的流式传输，只有最后全部发完就可以
    // 读文件内容，发送给客户端

// 发送目录
int sendDir(int cfd, const char* dirname);

// 和客户端断开连接
int disConn(int cfd, int epfd);

// 通过文件名得到文件的content-type
const char* getFileType(const char* name);


#endif