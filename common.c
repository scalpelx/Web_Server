#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <memory.h>
#include "common.h"

void rio_readinitb(rio_t *rp, int fd)
{
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;
    while (nleft > 0) 
    {
        if ((nwritten = write(fd, bufp, nleft)) <= 0) 
        {
            if (errno == EINTR)
                nwritten = 0;
            else
                return -1;
        }
        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;
    while (rp->rio_cnt <= 0) 
    {
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) 
        {
            if (errno != EINTR)
                return -1;
        }
        else if (rp->rio_cnt == 0)
            return 0;
        else
            rp->rio_bufptr = rp->rio_buf;
    }
    cnt = n;
    if (rp->rio_cnt < n)
        cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}

ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    int n, rc;
    char c, *bufp = usrbuf;
    for (n = 1; n < maxlen; n++) 
    {
        if ((rc = rio_read(rp, &c, 1)) == 1) 
        {
            *bufp++ = c;
            if (c == '\n')
                break;
        } 
        else if (rc == 0) 
        {
            if (n == 1)
                return 0;
            else
                break;
        } 
        else
            return -1;
    }
    *bufp = 0;
    return n;
}

ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;
    while (nleft > 0) 
    {
        if ((nread = rio_read(rp, bufp, nleft)) < 0) 
            return -1;          
        else if (nread == 0)
            break;             
        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft); 
}

void P(sem_t *sem) 
{
    if (sem_wait(sem) == -1)
        perror("P error");
}

void V(sem_t *sem) 
{
    if (sem_post(sem) == -1)
        perror("V error");
}

void sbuf_init(sbuf_t *sp, int cnt)
{
    sp->buf = calloc(cnt, sizeof(int)); 
    if (sp->buf == NULL)
    {
        perror("Calloc error");
        exit(1);
    }
    sp->cnt = cnt;                      
    sp->begin = sp->end = 0;       
    if (sem_init(&sp->mutex, 0, 1) == -1)      
    {
        perror("Sem_init error");
        exit(1);
    }
    if (sem_init(&sp->slots, 0, cnt) == -1)     
    {
        perror("Sem_init error");
        exit(1);
    }
    if (sem_init(&sp->items, 0, 0) == -1)    
    {
        perror("Sem_init error");
        exit(1);
    }
}

void sbuf_destroy(sbuf_t *sp)
{
    free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);                          
    P(&sp->mutex);                         
    sp->buf[(++sp->end) % (sp->cnt)] = item;   
    V(&sp->mutex);                         
    V(&sp->items);                         
}

int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);                          
    P(&sp->mutex);                          
    item = sp->buf[(++sp->begin) % (sp->cnt)];  
    V(&sp->mutex);                         
    V(&sp->slots);                         
    return item;
}
