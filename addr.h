#ifndef XK_ADDR_H
#define XK_ADDR_H

#include "xk.h"

#define XK_ADDR_MAXADDRINFO 32
#define XK_ADDR_HOSTNAME "61.129.42.75"
#define XK_ADDR_SRVNAME "80"

struct addrinfo *get_addr(int id);
int get_addr_cnt();
void free_addr();
void query_addr(const char *hostname, const char *srvname);
int straddr(const struct addrinfo *aip, char *buf, int buflen);

#endif

