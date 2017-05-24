#ifndef COMMON_H
#define COMMON_H

#include <semaphore.h>
#define MAXLINE 4096
#define SBUFSIZE 16
#define NTHREADS 4

typedef struct 
{
    int rio_fd;
    int rio_cnt;
    char *rio_bufptr;
    char rio_buf[MAXLINE];
} rio_t;

typedef struct
{
    int *buf;
    int cnt;
    int begin;
    int end;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

extern char **environ;

void rio_readinitb(rio_t *rp, int fd);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
ssize_t	rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);

void sbuf_init(sbuf_t *sp, int cnt);
void sbuf_destroy(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

#endif
