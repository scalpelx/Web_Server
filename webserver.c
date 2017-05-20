#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

void work(int fd);
void print_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
bool analyse_uri(char *uri, char *filename, char *cgiargs);
void static_serve(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void dynamic_serve(int fd, char *filename, char *cgiargs);

int main(int argc, char* argv[]) 
{
	//判断参数
    if (argc != 2) 
    {
        perror("usage: webserver <port>");
        exit(1);
    }
    //分配套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) 
    {
        perror("Can't allocate sockfd");
        exit(1);
    }
    //设置服务器套接字地址
    struct sockaddr_in clientaddr, serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(atoi(argv[1]));
    //绑定并监听
    if (bind(sockfd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) == -1)
    {
        perror("Bind Error");
        exit(1);
    }
    if (listen(sockfd, 4096) == -1)
    {
        perror("Listen Error");
        exit(1);
    }
    while (true) 
    {
        socklen_t clientlen = sizeof(clientaddr);
        //建立连接
        int connfd = accept(sockfd, (struct sockaddr *) &clientaddr, &clientlen);
        if (connfd == -1)
        {
            perror("Connect Error");
            exit(1);
        }
        printf("Accepted connection from (%s:%s)\n", inet_ntoa(clientaddr.sin_addr), argv[1]);
        //处理连接请求
        work(connfd);
        close(connfd);
    }
}
void work(int fd) 
{
    char buf[MAXLINE] = {0};
    rio_t rio;
    rio_readinitb(&rio, fd);
    //读取并解析请求行
    if (rio_readlineb(&rio, buf, MAXLINE) == -1) 
    {
        perror("Read error");
        exit(1);
    }
    puts("Request headers:");
    puts(buf);
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) 
    {
        client_error(fd, method, "501", "Not implemented", "The server does not implement this method");
        return;
    }
    //读取并忽略请求报头
    if (rio_readlineb(rio, buf, MAXLINE) == -1) 
    {
        perror("Read error");
        exit(1);
    }
    while (strcmp(buf, "\r\n")) //判断是否是报头最后的空行
    {
        if (rio_readlineb(rio, buf, MAXLINE) == -1) 
        {
            perror("Read error");
            exit(1);
        }
        printf("%s", buf);
    }
    //将URI解析为文件名和参数串并判断提供何种内容
    char filename[MAXLINE] = {0}, cgiargs[MAXLINE] = {0};
    bool is_static = analyse_uri(uri, filename, cgiargs);
    struct stat sbuf;
    if (stat(filename, &sbuf) < 0) 
    {
        client_error(fd, filename, "404", "Not found", "The server could not find this file");
        return;
    }
    if (is_static) 
    {
        if (!(S_ISREG(sbuf.st_mode)) && !(S_IRUSR & sbuf.st_mode)) 
        {
            client_error(fd, filename, "403", "Forbidden", "The server could not read this file");
            return;
        }
        static_serve(fd, filename, sbuf.st_size);
    }
    else //动态内容
    {
        if (!(S_ISREG(sbuf.st_mode)) && !(S_IXUSR & sbuf.st_mode)) 
        {
            client_error(fd, filename, "403", "Forbidden", "The server could not run the CGI program");
            return;
        }
        dynamic_serve(fd, filename, cgiargs);
    }
}
//发送HTTP响应来解释错误，将状态码和状态消息发送给客户
void print_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE] = {0}, body[MAXLINE] = {0};
    //输出HTTP响应
    if (rio_writen(fd, buf, strlen(buf)) == -1) 
    {
        perror("Write error");
        exit(-1);
    }
    sprintf(buf, "Content-type: text/html\r\n");
    if (rio_writen(fd, buf, strlen(buf)) == -1) 
    {
        perror("Write error");
        exit(-1);
    }
    sprintf(buf, "Content-length: %d\r\n\r\n", strlen(body));
    if (rio_writen(fd, buf, strlen(buf)) == -1) 
    {
        perror("Write error");
        exit(-1);
    }
    //建立并输出HTTP响应体
    sprintf(body, "<html><title>Server Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Web Server</em></body></html>\r\n", body);
    sprintf(buf, "HTTP/1.0 %s %s \r\n", errnum, shortmsg);
    if (rio_writen(fd, body, strlen(body)) == -1) 
    {
        perror("Write error");
        exit(-1);
    }
}
bool analyse_uri(char *uri, char *filename, char *cgiargs) 
{
	//默认可执行文件主目录为cgi
    if (!strstr(uri, "cgi")) //静态内容
    {
        strcpy(cgiargs, ""); //清空参数字符串
        strcpy(filename, ".");
        strcat(filename, uri); //将uri转为相对路径名
        if (uri[strlen(uri) - 1] == '/') //如果uri以/结尾，则将默认文件名加在后面
            strcat(filename, "index.html");
        return true;
    }
    else 
    {
        char *ptr = index(uri, '?'); //找到文件名与参数字符串分隔符
        if (ptr) 
        {
            strcpy(cgiargs, ptr + 1); //提前参数字符串
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);	//将uri剩下的部分转为相对路径名
        return false;
    }
}
void static_serve(int fd, char *filename, int filesize) 
{
    char buf[MAXLINE] = {0}, filetype[MAXLINE] = {0};
    get_filetype(filename, filetype); //获得文件类型
    //给客户端发送HTTP响应头
    sprintf(buf, "HTTP/1.0 200  OK\r\n");
    sprintf(buf, "%sServer: The Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    if (rio_writen(fd, buf, strlen(buf)) == -1) 
    {
        perror("Write error");
        exit(1);
    }
    puts("Response headers:");
    printf("%s", buf);
    //打开请求文件
    int srcfd = open(filename, O_RDONLY, 0);
    if (srcfd < 0 ) 
    {
        perror("Can't open the file");
        exit(1);
    }
    //将请求文件内容映射到一个虚拟内存空间
    char *srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    if (srcp == ((void *) -1)
    {
    	perror("mmap error");
    	exit(1);
    }
    close(srcfd);
    //将内容发送到客户端
    if (rio_writen(fd, srcp, filesize) == -1) 
    {
        perror("Write error");
        exit(1);
    }
    //释放虚拟内存
    munmap(srcp, filesize);
}
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpg");
    else
        strcpy(filetype, "text/plain");
}
void dynamic_serve(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE] = {0}, *emptylist[] = {NULL};
    //向客户端发送响应行
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    if (rio_writen(fd, buf, strlen(buf)) == -1) 
    {
        perror("Write error");
        exit(1);
    }
    sprintf(buf, "Server: The Web Server\r\n");
    if (rio_writen(fd, buf, strlen(buf)) == -1) 
    {
        perror("Write error");
        exit(1);
    }
    if (fork() == 0) //Child
    {
        setenv("QUERY_STRING", cgiargs, 1); //用CGI参数初始化环境变量
        dup2(fd, STDOUT_FILENO);
        execve(filename, emptylist, environ); //执行CGI程序
    }
    wait(NULL); //父进程阻塞
}