#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

void* thread(void *vargp);
void work(int fd);
void print_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_get_requesthdrs(rio_t *rp);
void read_post_requesthdrs(rio_t *rp, char *content);
bool analyse_uri(char *uri, char *filename, char *cgiargs);
void serve_dir(int fd,char *dirpath);
void static_serve(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void dynamic_serve(int fd, char *filename, char *cgiargs);

sbuf_t sbuf; //缓冲区

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
    sbuf_init(&sbuf, SBUFSIZE); //初始化缓冲区
    pthread_t tid;
    //创建线程
    for (int i = 0; i < NTHREADS; ++i)
        if (pthread_create(&tid, NULL, thread, NULL) != 0) 
        {
            perror("Create pthread error");
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
        char addr[INET_ADDRSTRLEN];
        printf("Accepted connection from (%s:%s)\n", inet_ntop(AF_INET, &clientaddr.sin_addr, addr, INET_ADDRSTRLEN), argv[1]);
        sbuf_insert(&sbuf, connfd); //将当前已连接套接字添加到缓存中
    }
    sbuf_destroy(&sbuf);
}

void* thread(void *vargp)
{
    if (pthread_detach(pthread_self()) != 0)  //分离当前线程
    {
        perror("Detach pthread error");
        exit(1);
    }
    while (true)
    {
        int connfd = sbuf_remove(&sbuf);  //从缓存中取可连接的套接字
        //处理连接请求
        work(connfd);
        close(connfd);
    }
    return NULL;
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
    //判断请求方式
    bool isGET = !strcasecmp(method, "GET"), isPOST = !strcasecmp(method, "POST");
    if (!isGET && !isPOST) 
    {
        print_error(fd, method, "501", "Not implemented", "The server does not implement this method");
        return;
    }
    //读取请求报头
    char content[MAXLINE] = {0};
    if (isGET)
        read_get_requesthdrs(&rio);
    else
        read_post_requesthdrs(&rio, content);
    //将URI解析为文件名和参数串并判断提供何种内容
    char filename[MAXLINE] = {0}, cgiargs[MAXLINE] = {0};
    bool is_static = analyse_uri(uri, filename, cgiargs);
    struct stat sbuf;
    if (stat(filename, &sbuf) < 0) 
    {
        print_error(fd, filename, "404", "Not found", "The server could not find this file");
        return;
    }
    if(S_ISDIR(sbuf.st_mode))
        serve_dir(fd, filename); //显示目录内容
    else if (is_static) 
    {
        if (isPOST)
        {
            print_error(fd, filename, "405", "Method Not Allowed", "Request method POST is not allowed for the URL");
            return;
        }
        if (!(S_ISREG(sbuf.st_mode)) && !(S_IRUSR & sbuf.st_mode)) 
        {
            print_error(fd, filename, "403", "Forbidden", "The server could not read this file");
            return;
        }
        static_serve(fd, filename, sbuf.st_size);
    }
    else //动态内容
    {
        if (!(S_ISREG(sbuf.st_mode)) && !(S_IXUSR & sbuf.st_mode)) 
        {
            print_error(fd, filename, "403", "Forbidden", "The server could not run the CGI program");
            return;
        }
        if (isPOST)
            strcpy(cgiargs, content);
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
    sprintf(buf, "Content-Type: text/html\r\n");
    if (rio_writen(fd, buf, strlen(buf)) == -1) 
    {
        perror("Write error");
        exit(-1);
    }
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
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

void read_get_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];
    if (rio_readlineb(rp, buf, MAXLINE) == -1) 
    {
        perror("Read error");
        exit(1);
    }
    while (strcmp(buf, "\r\n")) 
    {
        if (rio_readlineb(rp, buf, MAXLINE) == -1) 
        {
            perror("Read error");
            exit(1);
        }
        printf("%s", buf);
    }
}

void read_post_requesthdrs(rio_t *rp, char *content)
{
    char buf[MAXLINE];
    int contentlength = 0;
    if (rio_readlineb(rp, buf, MAXLINE) == -1) 
    {
        perror("Read error");
        exit(1);
    }
    while (strcmp(buf, "\r\n")) 
    {
        if (rio_readlineb(rp, buf, MAXLINE) == -1) 
        {
            perror("Read error");
            exit(1);
        }
        printf("%s", buf);
        if (strstr(buf, "Content-Length: ") == buf)
            contentlength = atoi(buf + strlen("Content-Length: ")); //获得长度
    }
    //读入报文体
    int n;
    if ((n = rio_readnb(rp, content, contentlength)) != contentlength)
    {
        perror("Read POST content error");
        if (n == -1)
            exit(1);
        contentlength = n;
    }
    content[contentlength] = '\0';
    puts(content);
}

bool analyse_uri(char *uri, char *filename, char *cgiargs) 
{
    //默认可执行文件主目录为cgi
    if (!strstr(uri, "cgi")) //静态内容
    {
        strcpy(cgiargs, ""); //清空参数字符串
        strcpy(filename, ".");
        strcat(filename, uri); //将uri转为相对路径名
        if (strlen(uri) == 0 || (strlen(uri) == 1 && uri[0] == '/')) //如果uri以/结尾，则将默认文件名加在后面
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

//目录显示
void serve_dir(int fd, char *dirpath)
{  
    char *p = strrchr(dirpath, '/');
    ++p;
    char dir[MAXLINE] = {0};
    strcpy(dir, p); //复制目录名
    strcat(dir, "/");
    DIR *dp;
    if((dp = opendir(dirpath)) == NULL)
    {
        perror("Cann't open the dir");
        exit(1);
    }
    char fbuf[MAXLINE] = {0};
    sprintf(fbuf, "<html><title>Display directory content</title>");
    sprintf(fbuf, "%s<body bgcolor=""ffffff"" font-family=Consolas><table cellspacing=""10"">\r\n", fbuf);
    struct dirent *dirp;
    while((dirp = readdir(dp)) != NULL) //遍历目录
    {
        if(!strcmp(dirp->d_name, ".") || !strcmp(dirp->d_name, ".."))
            continue;
        char filepath[MAXLINE] = {0};
        sprintf(filepath, "%s/%s", dirpath, dirp->d_name);
        struct stat sbuf;
        stat(filepath, &sbuf);
        char mtime[26] = {0};
        ctime_r(&sbuf.st_mtime, mtime);
        sprintf(fbuf,"%s<tr><td><a href=%s%s>%s</a></td><td>%ld</td><td>%s</td></tr>\r\n",
                fbuf, dir, dirp->d_name, dirp->d_name, sbuf.st_size, mtime);
    }
    closedir(dp);
    sprintf(fbuf,"%s</table></body></html>\r\n", fbuf);
    char buf[MAXLINE] = {0};
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: The Web Server\r\n", buf);
    sprintf(buf, "%sContent-Tength: %lu\r\n", buf, strlen(buf));
    sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, "text/html");
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, fbuf, strlen(fbuf));
}

//静态服务
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
    if (srcp == (void *)-1)
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

//获取文件类型
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

//动态服务
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