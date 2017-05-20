#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

void work(int fd);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *rp);
bool parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        perror("usage: webserver <port>");
        exit(1);
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Can't allocate sockfd");
        exit(1);
    }
    struct sockaddr_in clientaddr, serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(atoi(argv[1]));
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
    while (true) {
        socklen_t clientlen = sizeof(clientaddr);
        int connfd = accept(sockfd, (struct sockaddr *) &clientaddr, &clientlen);
        if (connfd == -1)
        {
            perror("Connect Error");
            exit(1);
        }
        printf("Accepted connection from (%s:%s)\n", inet_ntoa(clientaddr.sin_addr), argv[1]);
        work(connfd);
        close(connfd);
    }
}

void work(int fd) {
    char buf[MAXLINE];
    rio_t rio;
    rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) == -1) {
        perror("Read error");
        exit(1);
    }
    puts("Request headers:");
    puts(buf);
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        client_error(fd, method, "501", "Not implemented", "The server does not implement this method");
        return;
    }
    read_requesthdrs(&rio);
    char filename[MAXLINE], cgiargs[MAXLINE];
    bool is_static = parse_uri(uri, filename, cgiargs);
    struct stat sbuf;
    if (stat(filename, &sbuf) < 0) {
        client_error(fd, filename, "404", "Not found", "The server could not find this file");
        return;
    }
    if (is_static) {
        if (!(S_ISREG(sbuf.st_mode)) && !(S_IRUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden", "The server could not read this file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size);
    }
    else {
        if (!(S_ISREG(sbuf.st_mode)) && !(S_IXUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden", "The server could not run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs);
    }
}
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXLINE];
    sprintf(body, "<html><title>Server Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Web Server</em></body></html>\r\n", body);
    sprintf(buf, "HTTP/1.0 %s %s \r\n", errnum, shortmsg);
    if (rio_writen(fd, buf, strlen(buf)) == -1) {
        perror("Write error");
        exit(-1);
    }
    sprintf(buf, "Content-type: text/html\r\n");
    if (rio_writen(fd, buf, strlen(buf)) == -1) {
        perror("Write error");
        exit(-1);
    }
    sprintf(buf, "Content-length: %d\r\n\r\n", strlen(body));
    if (rio_writen(fd, buf, strlen(buf)) == -1) {
        perror("Write error");
        exit(-1);
    }
    if (rio_writen(fd, body, strlen(body)) == -1) {
        perror("Write error");
        exit(-1);
    }
}
void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];
    if (rio_readlineb(rp, buf, MAXLINE) == -1) {
        perror("Read error");
        exit(1);
    }
    while (strcmp(buf, "\r\n")) {
        if (rio_readlineb(rp, buf, MAXLINE) == -1) {
            perror("Read error");
            exit(1);
        }
        printf("%s", buf);
    }
}
bool parse_uri(char *uri, char *filename, char *cgiargs) {
    if (!strstr(uri, "cgi")) {
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        if (uri[strlen(uri) - 1] == '/')
            strcat(filename, "home.html");
        return true;
    }
    else {
        char *ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        return false;
    }
}
void serve_static(int fd, char *filename, int filesize) {
    char buf[MAXLINE], filetype[MAXLINE];
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200  OK\r\n");
    sprintf(buf, "%sServer: The Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    if (rio_writen(fd, buf, strlen(buf)) == -1) {
        perror("Write error");
        exit(1);
    }
    puts("Response headers:");
    printf("%s", buf);
    int srcfd = open(filename, O_RDONLY, 0);
    if (srcfd < 0 ) {
        perror("Can't open the file");
        exit(1);
    }
    char *srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close(srcfd);
    if (rio_writen(fd, srcp, filesize) == -1) {
        perror("Write error");
        exit(1);
    }
    munmap(srcp, filesize);
}
void get_filetype(char *filename, char *filetype) {
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
void serve_dynamic(int fd, char *filename, char *cgiargs) {
    char buf[MAXLINE], *emptylist[] = {NULL};
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    if (rio_writen(fd, buf, strlen(buf)) == -1) {
        perror("Write error");
        exit(1);
    }
    sprintf(buf, "Server: The Web Server\r\n");
    if (rio_writen(fd, buf, strlen(buf)) == -1) {
        perror("Write error");
        exit(1);
    }
    if (fork() == 0) {
        setenv("QUERY_STRING", cgiargs, 1);
        dup2(fd, STDOUT_FILENO);
        execve(filename, emptylist, environ);
    }
    wait(NULL);
}