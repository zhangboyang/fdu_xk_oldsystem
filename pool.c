#include <unistd.h>     /* close */
#include <errno.h>      /* errno */
#include <sys/select.h> /* select */
#include <sys/socket.h> /* struct addrinfo */
#include <netdb.h>      /* struct addrinfo */
#include <sys/time.h>   /* struct timespec */
#include <pthread.h>    /* pthread_t pthread_mutex_t */
#include <stdlib.h>     /* malloc free */
#include <string.h>     /* memcpy */

#include "xk.h"
#include "sleep.h"
#include "error.h"
#include "pool.h"
#include "fflag.h"
#include "ts.h"

/* useful functions */

static char chrstat(xk_pool_conn_stat x)
{
    const char *cs = XK_KEEPER_CHRSTAT;
    return cs[x];
}

static void swap_conn_ptr(xk_pool_conn **a, xk_pool_conn **b)
{
    xk_pool_conn *t;
    t = *a, *a = *b, *b = t;
}

static int xk_pool_conn_better(const xk_pool_conn *a, const xk_pool_conn *b)
{
    /* return 1 if a is better than b */
    /* first check cnt: less is better */
    if (a->cnt != b->cnt) return a->cnt < b->cnt;
    /* second check deadline: later is better */
    return !ts_less(&a->ts, &b->ts);
}

static int xk_pool_conn_compare(const void *a, const void *b)
{
    if (xk_pool_conn_better(*(const xk_pool_conn **) a,
                            *(const xk_pool_conn **) b)) return -1;
    if (xk_pool_conn_better(*(const xk_pool_conn **) b,
                            *(const xk_pool_conn **) a)) return 1;
    return 0;
}

static void update_strstat(xk_pool *arg)
{
    /* note: lock mutex before calling this function */
    int i;
    for (i = 0; i < arg->pool_total; i++) {
        arg->strstat[i] = chrstat(arg->pool[i]->stat);
        if (i < arg->pool_idle || i >= arg->pool_idle + arg->pool_active)
            arg->strstat[i] += 'a' - 'A';
    }
}

static xk_pool *xk_pool_malloc(int pool_total)
{
    xk_pool *ret;
    int i;
    
    if ((ret = malloc(sizeof(xk_pool))) == NULL)
        err_sys("malloc error");
    if ((ret->strstat = malloc(pool_total)) == NULL)
        err_sys("malloc error");
    if ((ret->mutex = malloc(sizeof(pthread_mutex_t))) == NULL)
        err_sys("malloc error");
    if (pthread_mutex_init(ret->mutex, NULL) != 0)
        err_quit("pthread_mutex_init error");
    if ((ret->pool = malloc(sizeof(xk_pool_conn *) * pool_total)) == NULL)
        err_sys("malloc error");
    for (i = 0; i < pool_total; i++) {
        if ((ret->pool[i] = malloc(sizeof(xk_pool_conn))) == NULL)
            err_sys("malloc error");
        if ((ret->pool[i]->recvbuf = malloc(XK_KEEPER_BUFSIZE)) == NULL)
            err_sys("malloc error");
    }
    
    return ret;
}

static void xk_pool_free(xk_pool *ptr)
{
    int i;
    for (i = 0; i < ptr->pool_total; i++) {
        free(ptr->pool[i]->recvbuf);
        free(ptr->pool[i]);
    }
    if (pthread_mutex_destroy(ptr->mutex) != 0)
        err_quit("pthread_mutex_destroy error");
    free(ptr->strstat);
    free(ptr->mutex);
    free(ptr->pool);
    free(ptr);
}

/* pool keeper */

static void keeper_connect(xk_pool *arg)
{
    xk_pool_conn **pool = arg->pool + arg->pool_idle;
    
    xk_pool_conn *cp;
    int i, cnt, ret;
    int fd;
    
    for (i = arg->pool_active; i < arg->pool_size; i++) {
        cp = pool[i];
        if (ts_less(&cp->ts, &arg->ts)) {
            for (cnt = 0; cnt < XK_KEEPER_MAXCONNTRY; cnt++) {
                // ugly patch start, achieve load banance to 2 ip addrs
                extern struct addrinfo *pool_addr[2];
                static int addrid = -1;
                extern int pool_addr_cnt;
                addrid = (addrid + 1) % pool_addr_cnt;
                //addrid=1;
                const struct addrinfo *addr = pool_addr[addrid];
                //char addrbuf[128]; straddr(addr, addrbuf, sizeof(addrbuf));
                //puts(addrbuf);
                //printf("cnt=%d id=%d\n", pool_addr_cnt, addrid);
                // ugly patch end
                if ((fd = socket(addr->ai_family, SOCK_STREAM, 0)) < 0)
                    err_sys("socket error");
                set_nonblock(fd);
                ret = connect(fd, addr->ai_addr, addr->ai_addrlen);
                if (ret == 0 || (ret < 0 && errno == EINPROGRESS))
                    break;
                close(fd);
            }
            if (cnt >= XK_KEEPER_MAXCONNTRY)
                err_sys("try limit exceed, connect error");
            cp->fd = fd;
            cp->stat = XK_POOL_CONN_CONNECTING;
            cp->sendlen = 0;
            cp->cnt = 0;
            arg->conn_cnt++;
            swap_conn_ptr(&pool[i], &pool[arg->pool_active++]);
        }
    }
}

static void keeper_recv(xk_pool *arg)
{
    xk_pool_conn **pool = arg->pool + arg->pool_idle;
    char *buf;
    xk_pool_conn *cp;
    int i, ret, len;
    int fd, maxfd;
    fd_set rset;
    struct timeval tv;
    
    tv.tv_sec = XK_KEEPER_SELECTDELAY / 1000;
    tv.tv_usec = (XK_KEEPER_SELECTDELAY % 1000) * 1000;
    maxfd = 0; FD_ZERO(&rset);
    for (i = 0; i < arg->pool_active; i++)
        if (pool[i]->stat == XK_POOL_CONN_WAITING) {
            fd = pool[i]->fd;
            FD_SET(fd, &rset);
            if (fd > maxfd) maxfd = fd;
        }
    
    ret = select(maxfd + 1, &rset, NULL, NULL, &tv); /* block */
    if (ret < 0) err_sys("select error");
    clock_gettime(CLOCK_REALTIME, &arg->ts); /* refresh current time */
    
    for (i = 0; i < arg->pool_active; i++) {
        cp = pool[i]; fd = cp->fd;
        if (cp->stat != XK_POOL_CONN_WAITING || !FD_ISSET(fd, &rset)) continue;
        buf = cp->recvbuf + cp->recvlen;
        len = XK_KEEPER_BUFSIZE - cp->recvlen;
        if (len <= 0) err_quit("recv buffer too small");
        ret = recv(fd, buf, len, 0); /* should not block */
        if (ret <= 0) { /* error occured, need cleanup */
            cp->stat = XK_POOL_CONN_CLEANUP;
            continue;
        }
        cp->recvlen += ret;
        if (find_http_header(cp->recvbuf, cp->recvlen) >= 0) {
            /* now have full http response */
            cp->stat = XK_POOL_CONN_IDLE;
            cp->sendlen = 0;
            ts_plus(&cp->ts, &arg->ts, XK_KEEPER_HEARTBEATINTERVAL);
        }
    }
}

static void keeper_sort(xk_pool *arg)
{
    xk_pool_conn **pool = arg->pool;
    xk_pool_conn *cp;
    int ptr1 = 0, ptr2 = 0, stolen_cnt = 0;
    int i;
    
    pthread_mutex_lock(arg->mutex); /* enter critical section */
    clock_gettime(CLOCK_REALTIME, &arg->ts); /* refresh current time */

    /* rearrange connections */
    for (i = 0; i < arg->pool_total; i++) {
        cp = pool[i];
        if (cp->stat == XK_POOL_CONN_STOLEN) {
            swap_conn_ptr(&pool[i], &pool[ptr2++]);
            stolen_cnt++;
        }
        if (cp->stat == XK_POOL_CONN_IDLE) {
            if (ts_less(&cp->ts, &arg->ts))
                swap_conn_ptr(&pool[i], &pool[ptr2++]);
            else {
                swap_conn_ptr(&pool[i], &pool[ptr2]);
                swap_conn_ptr(&pool[ptr1++], &pool[ptr2++]);
            }
        }
    }
    
    /* update pool information */
    arg->pool_active = arg->pool_idle + arg->pool_active - ptr1;
    arg->pool_idle = ptr1;
    arg->pool_size = arg->pool_total - ptr1;
    
    /* update mutex protected counters */
    arg->conn_cnt2 = arg->conn_cnt;
    arg->close_cnt2 = arg->close_cnt;
    
    /* update pool stat string */
    update_strstat(arg);
    
    pthread_mutex_unlock(arg->mutex); /* leave critical section */
}

static void keeper_send(xk_pool *arg)
{
    xk_pool_conn **pool = arg->pool + arg->pool_idle;
    const char *buf;
    xk_pool_conn *cp;
    int i, ret, len;
    int fd, maxfd;
    fd_set wset;
    struct timeval tv;
    
    tv.tv_sec = 0, tv.tv_usec = 0;
    maxfd = 0; FD_ZERO(&wset);
    for (i = 0; i < arg->pool_active; i++) {
        cp = pool[i];
        /* deadline of idle must be over because of sort */
        if (cp->stat == XK_POOL_CONN_CONNECTING ||
            cp->stat == XK_POOL_CONN_IDLE) {
            fd = cp->fd;
            FD_SET(fd, &wset);
            if (fd > maxfd) maxfd = fd;
        }
    }
    ret = select(maxfd + 1, NULL, &wset, NULL, &tv);
    if (ret < 0) err_sys("select error");
    
    for (i = 0; i < arg->pool_active; i++) {
        cp = pool[i]; fd = cp->fd;
        if ((cp->stat != XK_POOL_CONN_CONNECTING &&
             cp->stat != XK_POOL_CONN_IDLE) ||
            !FD_ISSET(fd, &wset)) continue;
        if (cp->cnt >= XK_KEEPER_MAXHEARTBEAT) {
            cp->stat = XK_POOL_CONN_CLEANUP;
            continue;
        }
        buf = XK_KEEPER_HBHEADER + cp->sendlen;
        len = strlen(XK_KEEPER_HBHEADER) - cp->sendlen;
        ret = send(fd, buf, len, 0); /* should not block */
        if (ret < 0) { /* error occured, need cleanup */
            cp->stat = XK_POOL_CONN_CLEANUP;
            continue;
        }
        if (ret == len) { /* send completed */
            cp->stat = XK_POOL_CONN_WAITING;
            cp->recvlen = 0;
            cp->cnt++;
            ts_plus(&cp->ts, &arg->ts, XK_KEEPER_CONNTIMEOUT);
        } else { /* send not completed */
            cp->sendlen += ret;
        }
    }
}

static void keeper_cleanup(xk_pool *arg)
{
    xk_pool_conn **pool = arg->pool + arg->pool_idle;
    xk_pool_conn *cp;
    int i;
    
    for (i = 0; i < arg->pool_active; i++) {
        cp = pool[i];
        if (cp->stat == XK_POOL_CONN_CLEANUP ||  /* cleanup required */
            cp->stat == XK_POOL_CONN_STOLEN ||   /* stolen by other thread */
            (cp->stat == XK_POOL_CONN_WAITING && /* waiting, timeout */
             ts_less(&cp->ts, &arg->ts))) {
            /* should not close stolen sockets */
            if (cp->stat != XK_POOL_CONN_STOLEN) {
                close(cp->fd);
                arg->close_cnt++;
            }
            /* choose later one for deadline */
            cp->ts = ts_less(&arg->ts, &arg->cts) ? arg->cts : arg->ts;
            ts_plus(&arg->cts, &cp->ts, XK_KEEPER_RECONNDELAY);
            /* set stat in case of close socket twice */
            cp->stat = XK_POOL_CONN_CONNQUEUED;
            swap_conn_ptr(&pool[i--], &pool[--arg->pool_active]);
        }
    }
}

static void *pool_keeper(void *arg_data)
{
    xk_pool *arg = (xk_pool *) arg_data;
    int i;
    
    clock_gettime(CLOCK_REALTIME, &arg->ts);
    ts_plus(&arg->cts, &arg->ts, XK_KEEPER_CONNDELAY * arg->pool_total);
    for (i = 0; i < arg->pool_total; i++) {
        arg->pool[i]->stat = XK_POOL_CONN_CONNQUEUED;
        ts_plus(&arg->pool[i]->ts, &arg->ts, XK_KEEPER_CONNDELAY * i);
    }
    
    while (!arg->cancel) {
        keeper_connect(arg); /* check and make new connection */
        keeper_recv(arg);    /* do recv stuff, block */
        keeper_sort(arg);    /* sort pool, need mutex */
        keeper_send(arg);    /* do send stuff */
        keeper_cleanup(arg); /* clean up inactive connections */
    }
    
    for (i = 0; i < arg->pool_total; i++)
        if (arg->pool[i]->stat != XK_POOL_CONN_CONNQUEUED)
            close(arg->pool[i]->fd);
    
    return (void *) 0;
}

/* pool api */

xk_pool *create_pool(int pool_total, struct addrinfo *addr)
{
    xk_pool *ptr = xk_pool_malloc(pool_total);
    
    ptr->pool_idle = ptr->pool_active = 0;
    ptr->pool_size = ptr->pool_total = pool_total;
    ptr->conn_cnt = ptr->close_cnt = 0;
    ptr->conn_cnt2 = ptr->close_cnt2 = 0;
    ptr->stolen_cnt = 0;
    memset(ptr->strstat, 'X', pool_total);
    ptr->cancel = 0;
    ptr->addr = addr;
    
    if (pthread_create(&ptr->tid, NULL, pool_keeper, ptr) != 0)
        err_quit("pthread_create error");
    return ptr;
}

void cancel_pool(xk_pool *ptr)
{
    ptr->cancel = 1;
    pthread_join(ptr->tid, NULL);
    xk_pool_free(ptr);
}

int get_idle_cnt(xk_pool *ptr)
{
    int i, idle_cnt = 0;
    pthread_mutex_lock(ptr->mutex); /* enter critical section */
    for (i = 0; i < ptr->pool_idle; i++)
        if (ptr->pool[i]->stat == XK_POOL_CONN_IDLE)
            idle_cnt++;
    pthread_mutex_unlock(ptr->mutex); /* leave critical section */
    return idle_cnt;
}

void wait_pool(xk_pool *ptr, int idle_required)
{
    while (get_idle_cnt(ptr) < idle_required)
        sleep_ms(XK_KEEPER_SELECTDELAY);
}

int try_get_conn(xk_pool *ptr, int fd[], int cnt)
{
    int i, j;
    int idle_cnt = 0, ret = 0;
    pthread_mutex_lock(ptr->mutex); /* enter critical section */
    
    /* count idle sockets, should not use pool_idle directly
     * because socket may have been stolen */
    for (i = 0; i < ptr->pool_idle; i++)
        if (ptr->pool[i]->stat == XK_POOL_CONN_IDLE)
            idle_cnt++;
    
    /* check if we have enough sockets */
    if (idle_cnt < cnt) {
        ret = -1;
        goto done;
    }
    
    /* sort pool to choose better connection */
    qsort(ptr->pool, ptr->pool_idle,
          sizeof(xk_pool_conn *), xk_pool_conn_compare);
    
    /* put result to fd[] */
    for (i = 0, j = 0; j < cnt; i++) /* need ignore already stolen sockets */
        if (ptr->pool[i]->stat == XK_POOL_CONN_IDLE) {
            ptr->pool[i]->stat = XK_POOL_CONN_STOLEN;
            fd[j++] = ptr->pool[i]->fd;
        }
    
    /* update pool information */
    update_strstat(ptr);
    ptr->stolen_cnt += cnt;
    
done:
    pthread_mutex_unlock(ptr->mutex); /* leave critical section */
    return ret;
}

void get_conn(xk_pool *ptr, int fd[], int cnt)
{
    while (try_get_conn(ptr, fd, cnt) < 0); /* busy wait */
}

int try_get_single_conn(xk_pool *ptr)
{
    /* return socket fd if success, -1 if no connection avaliable */
    int i, ret, best = -1;
    pthread_mutex_lock(ptr->mutex); /* enter critical section */
    
    /* choost best connection */
    for (i = 0; i < ptr->pool_idle; i++)
        if (ptr->pool[i]->stat == XK_POOL_CONN_IDLE &&
            (best < 0 || xk_pool_conn_better(ptr->pool[i], ptr->pool[best])))
           best = i;
    
    /* check if we have idle sockets */
    if (best < 0) {
        ret = -1;
        goto done;
    }

    ptr->pool[best]->stat = XK_POOL_CONN_STOLEN;
    ret = ptr->pool[best]->fd;

    /* update pool information */
    update_strstat(ptr);
    ptr->stolen_cnt++;
    
done:
    pthread_mutex_unlock(ptr->mutex); /* leave critical section */
    return ret;
}

int get_single_conn(xk_pool *ptr)
{
    int ret;
    while ((ret = try_get_single_conn(ptr)) < 0);
    return ret;
}

int pool_status(xk_pool *ptr, char *buf, int buflen)
{
    if (buflen < ptr->pool_total + 1) return -1;
    pthread_mutex_lock(ptr->mutex); /* enter critical section */
    memcpy(buf, ptr->strstat, ptr->pool_total);
    buf[ptr->pool_total] = '\0';
    pthread_mutex_unlock(ptr->mutex); /* leave critical section */
    return ptr->pool_total;
}

void pool_counter(xk_pool *ptr, int *conn_cnt, int *close_cnt, int *stolen_cnt)
{
    pthread_mutex_lock(ptr->mutex); /* enter critical section */
    if (conn_cnt) *conn_cnt = ptr->conn_cnt2;
    if (close_cnt) *close_cnt = ptr->close_cnt2;
    if (stolen_cnt) *stolen_cnt = ptr->stolen_cnt;
    pthread_mutex_unlock(ptr->mutex); /* leave critical section */
}

