#ifndef XK_REFRESHER_H
#define XK_REFRESHER_H

#include "xk.h"


#define XK_REFRESHER_SELECTDELAY 5
#define XK_REFRESHER_SENDDELAY 0
#define XK_REFRESHER_REPORTINTERVAL 1000 /* report interval */
#define XK_REFRESHER_REQUESTPRESEND 3  /* how many requests per send */
#define XK_REFRESHER_REQUESTPRECONN 90 /* max total request per connection */


#define XK_REFRESHER_MAXCOURSECODELEN 32
#define XK_REFRESHER_MAXJSESSIONIDLEN 128

typedef enum {
    XK_REFRESHER_CONN_NEW,       /* new, not used */
    XK_REFRESHER_CONN_INITIAL,   /* need init xk_http */
    XK_REFRESHER_CONN_NORMAL,    /* normal operation */
    XK_REFRESHER_CONN_CLEANUP,   /* need cleanup */
    XK_REFRESHER_CONN_ERROR      /* error, need cleanup */
} xk_refresher_conn_stat;

typedef struct {
    xk_http *hp;
    xk_pool *pp;
    char *course_code;
    char *jsessionid;
    xk_refresher_conn_stat stat;
    int construct_flag;  /* == 1: need construct request header */
    int construct_id;    /* first newly constructed request id, start from 0 */
    int send_cnt;
    int recv_cnt;
    int last_v;          /* latest vacancy value */
    struct timespec next_send;
    struct timespec rts; /* last response recv timestamp */
} xk_refresher_conn;

typedef struct {
    xk_refresher_conn **conn;
    int conn_cnt;
    int conn_total;
    int hunger_last;
    int hunger_total;
    int recv_total;
    int recv_last;
    int conn_used;
    int conn_err;
    struct timespec sts; /* start time */
    struct timespec rts; /* next report time */
} xk_refresher;

xk_refresher *new_refresher(int conn_total);
void free_refresher(xk_refresher *ptr);
void cleanup_refresher(xk_refresher *rp);
void reuse_refresher(xk_refresher *rp);
int add_target(xk_refresher *rp, xk_pool *pp,
               const char *jsessionid, const char *course_code);
int run_refresher(xk_refresher *rp, int (*judge)(int), /* judge function */
                  char *course_buf, int buflen,  /* course code (for return) */
                  struct timespec *rtsp, /* last recv timestamp (for return) */
                  struct timespec *dtsp); /* timeout deadline */
#endif
