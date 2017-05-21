#ifndef COMMON_H
#define COMMON_H

#define MAXLINE 4096
typedef struct {
    int rio_fd;
    int rio_cnt;
    char *rio_bufptr;
    char rio_buf[MAXLINE];
} rio_t;
extern char **environ;
void rio_readinitb(rio_t *rp, int fd);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
ssize_t	rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);

#endif
