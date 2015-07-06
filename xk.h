#ifndef XK_H
#define XK_H

//DEBUG

//DEBUG
/*
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include "msg.h"
#include "sleep.h"
static void *my_malloc(int x)
{
    void *ret = malloc(x);
    memset(ret, 0x7f, x);
    return ret;
}
static int my_recv1(int fd, char *ptr, int len, int dummy)
{
    return recv(fd, ptr, 1, dummy);
}
static int my_recv2(int fd, char *ptr, int len, int dummy)
{
    int X = 5295;
    return recv(fd, ptr, X < len ? X : len, dummy);
}
static int my_recv3(int fd, char *ptr, int len, int dummy)
{
    printf("RECV2 fd=%d ptr=%p len=%d dummy=%d\n", fd, ptr, len, dummy);
    int L = 5295;
    len = len < L ? len : L;
    int x = 0;
    int ret, i;
    for (i = 1; i <= 20; i++) {
        ret = recv(fd, ptr, len, dummy);
        if (ret < 0 && errno != EAGAIN) return ret;
        if (ret < 0) ret = 0;
        x += ret;
        ptr += ret;
        len -= ret;
        if (len == 0) break;
        sleep_ms(100);
    }
    printf("ret=%d\n", ret);
    return x;
}*/
//DEBUG END
//#define malloc my_malloc
//#define recv my_recv2

//DEBUGEND


#endif

