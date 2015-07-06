#ifndef XK_ERROR_H
#define XK_ERROR_H

#include "xk.h"

#define XK_ERROR_MAXLINE 4096

void err_sys(const char *fmt, ...);
void err_quit(const char *fmt, ...);

#endif
