#ifndef XK_MSG_H
#define XK_MSG_H

#include "xk.h"

#define XK_MSG_MAXLINE 4096

/* time unit is ms */
#define XK_MSG_FLUSHINTERVAL 100

typedef enum {
    XK_MSG_DEBUG,
    XK_MSG_INFO,
    XK_MSG_NOTICE,
    XK_MSG_GOOD,
    XK_MSG_BAD,
    XK_MSG_ERROR
} xk_msg_type;

typedef struct xk_msg_node {
    char *str;
    xk_msg_type type;
    struct timespec ts;
    struct xk_msg_node *next;
} xk_msg;


void msg_flush();
void create_msg_flush_thread();
void cancel_msg_flush_thread();
void msg(xk_msg_type type, const char *fmt, ...);
void msg_dbg(const char *fmt, ...);
void msg_info(const char *fmt, ...);
void msg_notice(const char *fmt, ...);
void msg_good(const char *fmt, ...);
void msg_bad(const char *fmt, ...);
void msg_err(const char *fmt, ...);

#endif
