#include "server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>
#include <errno.h>
#include <sys/stat.h>
#include <strings.h>
#include <dirent.h>
#include <pthread.h>

int initListenFd(unsigned short port){
    // 1.创建监听的套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if(lfd == -1){
        perror("socket");
        return -1;
    }
    // 2.设置端口复用
    int opt = 1;
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(ret == -1){
        perror("setsockopt");
        return -1;
    }
    // 3.绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    if(ret == -1){
        perror("bind");
        return -1;
    }
    // 4.设置监听
    ret = listen(lfd, 128);
    if(ret == -1){
        perror("listen");
        return -1;
    }
    // 5.将得到的可用套接字返回给调用者
    return lfd;
}

int acceptConn(int lfd, int epfd){
    // 1.建立新连接
    int cfd = accept(lfd, NULL, NULL);
    if(cfd == -1){
        perror("accept");
        return -1;
    }
    // 2.设置通信文件描述符为非阻塞
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    int ret = fcntl(cfd, F_SETFL, flag);
    if(ret == -1){
        perror("fcntl");
        return -1;
    }
    // 3.通信的文件描述符添加到epoll模型中
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // 边沿模式
    ev.data.fd = cfd;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if(ret == -1){
        perror("epoll_ctl");
        return -1;
    }
    return 1;
}

// 最终得到10进制的整数
int hexit(char c){
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// 解码
// from: 要被转换的字符 -> 传入参数
// to: 转换之后得到的字符 -> 传出参数
void decodeMsg(char* to, char *from){
    for(; *from != '\0'; ++to, ++from){
        // isxdigit -> 判断字符是不是16进制格式
        if(from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])){
            // 将16进制的数 -> 10进制 将这个数值赋值给了字符 int -> char
            // A1 = 161
            *to = hexit(from[1]) * 16 + hexit(from[2]);
            from += 2;
        }else{
            // 不是特殊字符直接赋值
            *to = *from;
        }
    }
    *to = '\0';
}

int setNonBlocking(int cfd) {
    int flags = fcntl(cfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    if (fcntl(cfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}
// 发送文件
int sendFile(int cfd, const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return -1;
    }
    if (setNonBlocking(cfd) == -1) {
        close(fd);
        return -1;
    }
    char buf[1024] = {0};
    ssize_t bytesRead;
    ssize_t bytesSent;
    while ((bytesRead = read(fd, buf, sizeof(buf))) > 0) {
        size_t totalBytesSent = 0;
        while (totalBytesSent < bytesRead) {
            bytesSent = send(cfd, buf + totalBytesSent, bytesRead - totalBytesSent, MSG_NOSIGNAL);
            if (bytesSent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 发送缓冲区已满，稍等一会再尝试发送
                    usleep(1000);
                    continue;
                } else {
                    perror("send");
                    close(fd);
                    return -1;
                }
            }
            totalBytesSent += bytesSent;
        }
    }
    if (bytesRead == -1) {
        perror("read");
        close(fd);
        return -1;
    }
    close(fd);
    std::cout << "\033[01;32m- 文件已读完并发送完毕 -\033[00m" << std::endl;
    return 0;
}

// int sendFile(int cfd, const char* filename){
//     int fd = open(filename, O_RDONLY);
//     // 循环读文件
//     while(1){
//         char buf[1024] = {0};
//         int len = read(fd, buf, sizeof(buf));
//         if(len > 0){
//             // 发送读出的内容
//             send(cfd, buf, len, 0);
//             // 发送端发送太快会导致接收端的显示有异常
//             usleep(1000);
//         }else if(len == 0){
//             // 文件读完了
//             std::cout << "\033[01;32m- 文件已读完并发送完毕 -\033[00m" << std::endl;
//             break;
//         }else{
//             perror("read");
//             return -1;
//         }
//     }
//     return 0;
// }

// 客户端访问目录，服务器需要遍历当前目录，并且将目录中的所有文件名发送给客户端即可
// 遍历目录得到的文件名需要放到html的表格中
int sendDir(int cfd, const char* dirname){
    /*
    struct dirent **namelist;这个二级指针指向的时一个指针数组
    int scandir();
    参数：
        - drip: 要遍历的目录名
        - namelist: 三级指针，要传递二级指针的地址 -> 传出参数
        - filter: 回调函数，遍历目录时指定的过滤规则，不过滤则写NULL
        - compare: 回调函数，给遍历目录得到的文件名排序
            - alphasort: 根据文件名的ascii排序 -> 常用的方式
            - versionsort: 根据版本排序
    int alphasort(const struct dirent **a, const struct dirent **b);
    int versionsort(const struct dirent **a, const struct dirent **b);
    返回值：成功返回遍历的命令中的文件格式，失败返回-1
    */
    char buf[4096];
    struct dirent **namelist;
    sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirname);
    int num = scandir(dirname, &namelist, NULL, alphasort);
    for(int i = 0; i < num; i++){
        // 取出文件名
        char* name = namelist[i]->d_name;
        // 拼接当前文件在资源文件中的相对路径
        char subpath[1024];
        sprintf(subpath, "%s/%s", dirname, name);
        struct stat st;
        // stat函数的第一个参数文件的路径 ./xxx.txt
        int ret = stat(subpath, &st);
        if(ret == -1){
            perror("stat");
            return -1;
        }
        if(S_ISDIR(st.st_mode)){
            // 如果是目录，超链接的跳转路径名后边加/
            sprintf(buf+strlen(buf), "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>", name, name, (long)st.st_size);
        }else{
            sprintf(buf+strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", name, name, (long)st.st_size);
        }
        

        // 发送数据
        send(cfd, buf, strlen(buf), 0);

        // 清空数组
        memset(buf, 0, sizeof(buf));

        // 释放资源 namelist[i] 这个指针指向一块有效的内存
        free(namelist[i]);
    }
    // 补充html中剩余的标签
    sprintf(buf, "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    // 释放namelist
    free(namelist);
    return 0;
}

// status状态码，descr状态码描述
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length){
    // 状态行 + 消息报头 + 空行
    char buf[4096];
    // http/1.1 200 ok
    sprintf(buf, "HTTP/1.1 %d %s\r\n", status, descr);
    // 消息报头
    // content-type:xxx
    // content-length:xxx
    sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
    sprintf(buf + strlen(buf), "Content-Length: %d\r\n\r\n", length);
    
    // 拼接完成之后，发送
    send(cfd, buf, strlen(buf), 0);
    std::cout<< "发送" <<buf << std::endl;
    return 0;
}

int disConn(int cfd, int epfd){
    // 将cfd从epoll模型上删除
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if(ret == -1){
        perror("epoll_ctl");
        close(cfd);
        return -1;
    }
    close(cfd);
    std::cout << "--------------------------------------------------------" << std::endl;
    return 0;
}

// 通过文件名获取文件的类型
const char* getFileType(const char* name){
    const char* dot = strrchr(name, '.');
    if(dot == NULL) return "text/plain; charset=utf-8"; // 纯文本
    if(strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8"; 
    if(strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg"; 
    if(strcmp(dot, ".png") == 0  ) return "image/png";
    if(strcmp(dot, ".css") == 0  ) return "text/css";
    if(strcmp(dot, ".au") == 0  ) return "audio/basic";
    if(strcmp(dot, ".wav") == 0  ) return "audio/wav";
    if(strcmp(dot, ".avi") == 0  ) return "video/x-msvideo";
    if(strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0) return "video/quicktime";
    if(strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0) return "video/mpeg";
    if(strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0) return "model/vrml";
    if(strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0) return "audio/midi";
    if(strcmp(dot, ".mp3") == 0  ) return "audio/mpeg";
    if(strcmp(dot, ".ogg") == 0  ) return "application/ogg";
    if(strcmp(dot, ".pac") == 0  ) return "application/x-ns-proxy-autoconfig";
    return "text/plain; charset=utf-8";
}

int parseRequestLine(int cfd, const char* reqLine){
    // 请求行分为三部分
    // GET /hello/world/ http/1.1
    // 1.将请求行的三部分依次拆开，有用的前两部分
    // - 提交数据的方式
    // - 客户端向服务器请求的文件名
    char method[6];
    char path[1024];
    sscanf(reqLine, "%[^ ] %[^ ]", method, path);

    // 2.判断请求方式是不是GET，不是GET就直接忽略
    // http
    if(strcasecmp(method, "get") != 0){
        std::cout << "\033[01;31m- 用户提交的不是get, 忽略 -\033[00m" << std::endl;
        return -1;
    }else{
        std::cout << "\033[01;32m- 用户提交的get -\033[00m" << std::endl;
    }
    // 3.判断用户提交的请求是要访问服务器端的文件还是目录
    // /hello/world/
    // - 第一个/: 服务器的提供资源根目录，在服务器端可以随时指定
    // /hello/world/ -> 服务器资源根目录中的两个目录
    // 需要在程序中判断得到的文件的属性
    // 判断path中存储的是什么字符串

    char* file = NULL;          // file
    // 如果文件中有中文名，需要还原
    decodeMsg(path, path);
    if(strcmp(path, "/") == 0){
        
        // 访问的是服务器提供的资源根目录
        // / 不是系统根目录，是服务器提供各资源目录
        // 如何在服务器端将服务器资源根目录描述出来
        // - 在启动服务器程序时，先指定资源根目录是哪个
        // - 在main()函数中将工作目录切换到了资源根目录
        file = "./";        // ./对应的目录就是客户端访问的资源根目录
    }else{
        file = path + 1;    // hello/a.txt == ./hello/a.txt
    }
    // 属性判断
    std::cout << "\033[01;32m- 用户提交的文件名：" << file << " -\033[00m" << std::endl;
    struct stat st;
    // 第一个参数是文件的路径，相对/绝对，file中存储的是相对路径
    int ret = stat(file, &st);
    if(ret == -1){
        // 获取文件属性失败 ==> 没有这个文件
        // 给客户端发送404
        std::cout << "\033[01;31m- 用户提交的路径有误，获取文件或目录失败 -\033[00m" << std::endl;
        sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
        sendFile(cfd, "/home/fy/5_Projects/HTTP_Server/html/404.html");
    }else if(S_ISDIR(st.st_mode)){
        // 遍历目录，将目录内容发送给客户端
        // 5.客户端请求的名字是一个目录，发送目录内容给客户端
        std::cout << "\033[01;32m- 用户提交的路径为目录 -\033[00m" << std::endl;
        sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
        sendDir(cfd, file);   // <table></table>
    }else{
        // 如果是普通文件，发送文件内容给客户端
        // 4.客户端请求的名字是一个文件，发送文件内容给客户端
        std::cout << "\033[01;32m- 用户提交的路径为普通文件 -\033[00m" << std::endl;
        sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
        sendFile(cfd, file);
    }
    return 0;
}

int recvHttpRequest(int cfd, int epfd){
    // 因为是边沿非阻塞模式，所以需要循环读数据
    char tmp[1024]; // 每次接收1k数据
    char buf[4096]; // 将每次读到的数据存储到这个buf中
    // 循环读
    int len, total = 0; // total: 当前buf中已经存储了多少数据
    // 没有必要将所有的http请求全部保存下来
    // 因为需要的数据都在请求行中
    // - 客户端向服务器请求都是静态资源，请求的资源内容在请求行的第二部分
    // - 只需要将请求完整的保存下来就可以，请求行后边请求头和空行
    // - 不需要解析请求头中的数据，因此接收到 之后不存储也是没问题的
    while((len = recv(cfd, tmp, sizeof(tmp), 0)) > 0){
        if(total + len < sizeof(buf)){
            // 有空间存储数据
            memcpy(buf + total, tmp, len);
        }
        total += len;
    }
    std:: cout << buf << std::endl;
    // 循环结束 -> 读完了
    // 读操作是非阻塞的，当前缓存中没有数据返回-1
    if(len == -1 && errno == EAGAIN){
        // 将请求行从接收的数据中拿出来
        // 在http协议中换行使用的是 \r\n
        char* pt = strstr(buf, "\r\n");
        // 请求行字节数（长度）
        int reqlen = pt - buf;
        // 保留请求行就可以
        buf[reqlen] = '\0'; // 字符串截断
        // 解析请求行
        std::cout << "\033[01;32m- 解析请求行 -\033[00m" << std::endl;
        parseRequestLine(cfd, buf);
    }
    else if(len == 0){
        std::cout << "\033[01;31m- 客户端断开了连接 -\033[00m" << std::endl;
        disConn(cfd, epfd);
        // 服务器和客户端断开连接，文件描述符从epoll模型中删除
    }else{
        perror("recv");
        return -1;
    }
    return 0;
}
// 接收线程的回调函数
void* acceptThread(void* arg) {
    std::cout << "\033[01;32m- 建立连接的线程回调函数调用 -\033[00m" << std::endl;
    int lfd = *(int*)arg;
    int epfd = *((int*)arg + 1);
    acceptConn(lfd, epfd);
    pthread_exit(NULL);
}
// 通信线程的回调函数
void* recvThread(void* arg) {
    std::cout << "\033[01;32m- 接收请求的线程回调函数调用 -\033[00m" << std::endl;
    int curfd = *(int*)arg;
    int epfd = *((int*)arg + 1);
    recvHttpRequest(curfd, epfd);
    pthread_exit(NULL);
}

int epollRun(unsigned short port){
    // 1.创建epoll模型
    int epfd = epoll_create(10);
    if(epfd == -1){
        perror("epoll_create");
        return -1;
    }
    // 2.初始化epoll模型
    int lfd = initListenFd(port);
    while(lfd == -1){
        std::cout << "\033[01;31m- 监听建立失败，五秒后重试 -\033[00m" << std::endl;
        lfd = initListenFd(port);
        sleep(5);
    }
    std::cout << "\033[01;32m- 监听建立成功 -\033[00m" << std::endl;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    // 添加lfd到检测模型中
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if(ret == -1){
        perror("epoll_ctl");
        return -1;
    }else{
        std::cout << "\033[01;32m- 监听的文件描述符添加至epoll模型 -\033[00m" << std::endl;
    }

    // 检测 - 循环检测
    struct epoll_event evs[1024];
    int size = sizeof(evs)/sizeof(evs[0]);
    // while(1){
    //     // （可改进）主线程不停的调用epoll_wait
    //     int num = epoll_wait(epfd, evs, size, -1);
    //     for(int i = 0; i < num; i++){
    //         int curfd = evs[i].data.fd;
    //         if(curfd == lfd){
    //             // 建立新连接
    //             // （可改进）创建子线程，在子线程中建立新连接，acceptConn函数子线程的回调函数
    //             int ret = acceptConn(lfd, epfd);
    //             if(ret == -1){
    //                 // 规定：连接建立失败，直接终止程序
    //                 std::cout << "\033[01;31m- 连接失败，五秒后重连 -\033[00m" << std::endl;
    //                 sleep(5);
    //                 break;
    //             }else if(ret > 0){
    //                 std::cout << "\033[01;32m- 与客户端的连接已建立 -\033[00m" << std::endl;
    //             }
    //         }else{
    //             // 通信 -> 先接收数据，然后再回复数据
    //             // （可改进）创建子线程，在子线程中通信，recvHttpRequest函数是子线程的回调函数
    //             std::cout << "\033[01;32m- 开始通信 -\033[00m" << std::endl;
    //             recvHttpRequest(curfd, epfd);
    //         }
    //     }
    // }
    while (1) {
        // 主线程不停的调用epoll_wait
        int num = epoll_wait(epfd, evs, size, -1);
        for (int i = 0; i < num; i++) {
            int curfd = evs[i].data.fd;
            if (curfd == lfd) {
                // 建立新连接
                pthread_t accept_tid;
                int args[2] = {lfd, epfd};
                pthread_create(&accept_tid, NULL, acceptThread, (void*)args);
                pthread_detach(accept_tid);
            } else {
                // 通信 -> 先接收数据，然后再回复数据
                pthread_t recv_tid;
                int args[2] = {curfd, epfd};
                pthread_create(&recv_tid, NULL, recvThread, (void*)args);
                pthread_detach(recv_tid);
            }
        }
    }
    return 0;
}