#ifndef XK_FFLAG_H
#define XK_FFLAG_H

#include "xk.h"

void set_nonblock(int fd);
void set_block(int fd);
void set_tcp_nodelay(int fd);
void set_ip_tos(int fd);

#endif
