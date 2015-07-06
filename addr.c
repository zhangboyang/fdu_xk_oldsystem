#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include "xk.h"
#include "addr.h"
#include "error.h"

static struct addrinfo *addr[XK_ADDR_MAXADDRINFO];
static struct addrinfo *addr_query_result[XK_ADDR_MAXADDRINFO];
static int addr_len = 0;
static int addr_query_cnt = 0;

struct addrinfo *get_addr(int id) { return addr[id]; }
int get_addr_cnt() { return addr_len; }

void free_addr()
{
    while (addr_query_cnt--)
        freeaddrinfo(addr_query_result[addr_query_cnt]);
}

void query_addr(const char *hostname, const char *srvname)
{
    struct addrinfo *ailist, *aip;
    struct addrinfo hint;
    int err;
    
    hint.ai_flags = 0;
    hint.ai_family = 0;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;
    hint.ai_addrlen = 0;
    hint.ai_canonname = NULL;
    hint.ai_addr = NULL;
    hint.ai_next = NULL;
    err = getaddrinfo(hostname, srvname, &hint, &ailist);
    if (err != 0) err_quit("get addrinfo error: %s", gai_strerror(err));
    
    addr_query_result[addr_query_cnt++] = ailist;
    for (aip = ailist; aip != NULL; aip = aip->ai_next)
        if (addr_len < XK_ADDR_MAXADDRINFO)
            addr[addr_len++] = aip;
        else
            err_quit("too many addrinfo");
}

int straddr(const struct addrinfo *aip, char *buf, int buflen)
{
    struct sockaddr_in *sinp;
    struct sockaddr_in6 *sin6p;
    const char *addr;
    char abuf[INET_ADDRSTRLEN];
    char a6buf[INET6_ADDRSTRLEN];
    int ret;

    if (aip->ai_family == AF_INET) {
        sinp = (struct sockaddr_in *) aip->ai_addr;
        addr = inet_ntop(AF_INET, &sinp->sin_addr, abuf, INET_ADDRSTRLEN);
        ret = snprintf(buf, buflen, "inet address %s port %d", 
                       addr ? addr : "unknown", ntohs(sinp->sin_port));
    }
    else if (aip->ai_family == AF_INET6) {
        sin6p = (struct sockaddr_in6 *) aip->ai_addr;
        addr = inet_ntop(AF_INET6, &sin6p->sin6_addr, a6buf, INET6_ADDRSTRLEN);
        ret = snprintf(buf, buflen, "inet6 address %s port %d",
                       addr ? addr : "unknown", ntohs(sin6p->sin6_port));
    }
    else
        ret = snprintf(buf, buflen, "unknown");
    return ret;
}

