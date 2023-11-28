#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "server.h"

// 原则：main()函数只是逻辑函数调用，具体的内容不会写在这里面
// 功能函数：功能尽可能单一
int main(int argc, char* argv[]){
    // a.out port path
    if(argc < 3){
        std::cout << "./a.out port respath" << std::endl;
        exit(0);
    }
    // 资源根目录存储到argv[2]，假设：/home/robin/luffy
    // 将当前的服务器进程工作目录切换到资源根目录中
    chdir(argv[2]);
    // execlp("pwd", "pwd", NULL);
    // 启动服务器 -> 基于epoll
    unsigned short port = atoi(argv[1]);
    epollRun(port);
    return 0;
}