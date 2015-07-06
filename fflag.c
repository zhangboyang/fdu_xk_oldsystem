#include <fcntl.h> /* fcntl */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include "xk.h"
#include "error.h"
#include "fflag.h"

static void set_fl(int fd, int flags)
{
    int val;
    if ((val = fcntl(fd, F_GETFL, 0)) < 0)
        err_sys("fcntl F_GETFL error");
    if (fcntl(fd, F_SETFL, val | flags) < 0)
        err_sys("fcntl F_SETFL error");
}

static void clr_fl(int fd, int flags)
{
    int val;
    if ((val = fcntl(fd, F_GETFL, 0)) < 0)
        err_sys("fcntl F_GETFL error");
    if (fcntl(fd, F_SETFL, val & ~flags) < 0)
        err_sys("fcntl F_SETFL error");
}

void set_nonblock(int fd)
{
    set_fl(fd, O_NONBLOCK);
}

void set_block(int fd)
{
    clr_fl(fd, O_NONBLOCK);
}

void set_tcp_nodelay(int fd)
{
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
}

void set_ip_tos(int fd)
{
    int tos = IPTOS_LOWDELAY;
    setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(int));
}

