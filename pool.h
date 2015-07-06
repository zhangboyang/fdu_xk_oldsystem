#ifndef XK_POOL_H
#define XK_POOL_H

#include <sys/socket.h> /* struct addrinfo */
#include <netdb.h>      /* struct addrinfo */
#include <sys/time.h>   /* struct timespec */
#include <pthread.h>    /* pthread_t pthread_mutex_t */
#include "xk.h"
#include "http.h"

#define XK_KEEPER_MAXCONNTRY 20
#define XK_KEEPER_BUFSIZE 512
#define XK_KEEPER_MAXHEARTBEAT 30

#define XK_KEEPER_HBHEADER \
    "HEAD / HTTP/1.1\r\n" \
    "Host: " XK_HTTP_HOSTNAME "\r\n" \
    "User-Agent: " XK_HTTP_USERAGENT "\r\n" \
    "Connection: keep-alive\r\n" \
    "\r\n"

/* time unit is ms */
#define XK_KEEPER_SELECTDELAY 100
#define XK_KEEPER_HEARTBEATINTERVAL 7000
#define XK_KEEPER_CONNTIMEOUT 15000
#define XK_KEEPER_CONNDELAY 1000
#define XK_KEEPER_RECONNDELAY 50


/* must match defination of xk_pool_conn_stat */
#define XK_KEEPER_CHRSTAT "QCWISD"
typedef enum {
    XK_POOL_CONN_CONNQUEUED, /* waiting for connect */
    XK_POOL_CONN_CONNECTING, /* connecting, waiting socket for write */
    XK_POOL_CONN_WAITING,    /* connected, request sent, waiting for response */
    XK_POOL_CONN_IDLE,   /* connected, idle (request sent, response received) */
    XK_POOL_CONN_STOLEN, /* stolen by other thread, do not close socket */
    XK_POOL_CONN_CLEANUP /* need cleanup (error occured, or limit exceed) */
} xk_pool_conn_stat;


typedef struct {
    int fd;             /* socket fd */
    xk_pool_conn_stat stat;  /* connection status */
    int cnt;            /* heartbeat count */
    struct timespec ts; /* timeout deadline */
    int sendlen;        /* bytes sent */
    int recvlen;        /* bytes received */
    char *recvbuf;      /* recv buffer */
} xk_pool_conn;


typedef struct {
    xk_pool_conn **pool;    /* first pool_idle elements are mutex protected */
    int pool_idle;          /* mutex protected */
    int pool_active, pool_size, pool_total;
    /*
     * pool: [-------idle-------][-------non-idle-------][---need-cleanup---]
     *       |<---pool_idle---->||<----pool_active----->|
     *       |<-real_idle->|     |<---------------pool_size---------------->|
     *       |<------------------------pool_total-------------------------->|
     */
    int conn_cnt;           /* connect counter */
    int close_cnt;          /* close counter */
    int conn_cnt2;          /* mutex protected copy */
    int close_cnt2;         /* mutex protected copy */
    int stolen_cnt;         /* stolen counter, mutex protected */
    char *strstat;          /* pool status, not terminated, mutex protected */
    volatile int cancel;    /* set this to cancel pool thread */
    struct addrinfo *addr;  /* server addr to create connection pool */
    pthread_mutex_t *mutex; /* pool mutex */
    pthread_t tid;          /* pool thread id */
    struct timespec ts;     /* current time */
    struct timespec cts;    /* next new connection time */
} xk_pool;


xk_pool *create_pool(int pool_total, struct addrinfo *addr);
void cancel_pool(xk_pool *ptr);
int get_idle_cnt(xk_pool *ptr);
void wait_pool(xk_pool *ptr, int idle_required);
int try_get_conn(xk_pool *ptr, int fd[], int cnt);
void get_conn(xk_pool *ptr, int fd[], int cnt);
int try_get_single_conn(xk_pool *ptr);
int get_single_conn(xk_pool *ptr);
int pool_status(xk_pool *ptr, char *buf, int buflen);
void pool_counter(xk_pool *ptr, int *conn_cnt, int *close_cnt, int *stolen_cnt);

#endif
