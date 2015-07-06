#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>
#include "xk.h"
#include "error.h"
#include "http.h"
#include "pool.h"
#include "fduxk.h"
#include "ts.h"
#include "fflag.h"
#include "refresher.h"
#include "strlcpy.h"


//debug
#include <stdio.h>

static xk_refresher_conn *new_refresher_conn()
{
    xk_refresher_conn *ret;
    if ((ret = malloc(sizeof(xk_refresher_conn))) == NULL)
        err_sys("malloc error");
    if ((ret->course_code = malloc(XK_REFRESHER_MAXCOURSECODELEN)) == NULL)
        err_sys("malloc error");
    if ((ret->jsessionid = malloc(XK_REFRESHER_MAXJSESSIONIDLEN)) == NULL)
        err_sys("malloc error");
    ret->hp = xk_http_new();
    return ret;
}

static void free_refresher_conn(xk_refresher_conn *ptr)
{
    xk_http_free(ptr->hp);
    free(ptr->jsessionid);
    free(ptr->course_code);
    free(ptr);
}

xk_refresher *new_refresher(int conn_total)
{
    int i;
    xk_refresher *ret;
    if ((ret = malloc(sizeof(xk_refresher))) == NULL)
        err_sys("malloc error");
    if ((ret->conn = malloc(sizeof(xk_refresher_conn *) * conn_total)) == NULL)
        err_sys("malloc error");
    ret->conn_total = conn_total;
    for (i = 0; i < conn_total; i++) {
        ret->conn[i] = new_refresher_conn();
        ret->conn[i]->stat = XK_REFRESHER_CONN_NEW;
    }
    cleanup_refresher(ret);
    return ret;
}

void free_refresher(xk_refresher *ptr)
{
    int i;
    cleanup_refresher(ptr);
    for (i = 0; i < ptr->conn_total; i++)
        free_refresher_conn(ptr->conn[i]);
    free(ptr->conn);
    free(ptr);
}

void cleanup_refresher(xk_refresher *rp)
{
    int i;
    rp->conn_cnt = 0;
    rp->conn_used = rp->conn_err = 0;
    rp->recv_last = rp->recv_total = 0;
    rp->hunger_last = rp->hunger_total = 0;
    for (i = 0; i < rp->conn_total; i++)
        if (rp->conn[i]->stat != XK_REFRESHER_CONN_NEW) {
            close(rp->conn[i]->hp->fd);
            rp->conn[i]->stat = XK_REFRESHER_CONN_NEW;
        }
}

void reuse_refresher(xk_refresher *rp)
{
    rp->conn_cnt = 0;
}

int add_target(xk_refresher *rp, xk_pool *pp,
               const char *jsessionid, const char *course_code)
{
    /* return value: total conn count */
    xk_refresher_conn *rcp;
    if (rp->conn_cnt >= rp->conn_total) err_quit("too many targets");
    rcp = rp->conn[rp->conn_cnt];
    if (pp) {
        rcp->pp = pp;
        if (rcp->stat == XK_REFRESHER_CONN_NEW)
            rcp->stat = XK_REFRESHER_CONN_INITIAL;
    }
    rcp->construct_flag = 1;
    if (course_code)
        strlcpy(rcp->course_code, course_code, XK_REFRESHER_MAXCOURSECODELEN);
    if (jsessionid)
        strlcpy(rcp->jsessionid, jsessionid, XK_REFRESHER_MAXJSESSIONIDLEN);
    return rp->conn_cnt++;
}

static void construct_request(xk_refresher_conn *rcp)
{
    xk_http *hp = rcp->hp;
    if (!rcp->construct_flag) {
        reuse_request(hp); /* just reuse last header */
        return;
    }
    /* request header construction needed */
    init_request(hp, "GET", "/xk/sekcoursepeos.jsp?xkh=%s", rcp->course_code);
    add_header(hp, "Cookie: JSESSIONID=%s", rcp->jsessionid);
    finalize_request(hp);
    rcp->construct_flag = 0;
    rcp->construct_id = rcp->send_cnt;
}

static int check_vacancy(xk_refresher_conn *rcp)
{
    /* return value: >= 0 vacancy value
     *               <  0 not found yet */
    xk_http *hp = rcp->hp;
    int v;
    v = find_vacancy(hp, rcp->course_code, NULL, NULL);
    if (hp->response_dataflag == 2 && v == -1) v = 0;
    return v;
}

int run_refresher(xk_refresher *rp, int (*judge)(int), /* judge function */
                  char *course_buf, int buflen,  /* course code (for return) */
                  struct timespec *rtsp, /* last recv timestamp (for return) */
                  struct timespec *dtsp) /* timeout deadline */
{
    /* if jugde function return != 0, this function returns vacancy value
     * return value: >= 0 vacancy value
     *               <  0 timeout */
    xk_refresher_conn *rcp;
    xk_http *hp;
    fd_set rset, wset;
    struct timeval tv;
    struct timespec ts;
    int fd, maxfd;
    int v;
    int i, ret;
    
    /* cleanup unreused connections */
    for (i = rp->conn_cnt; i < rp->conn_total; i++)
        if (rp->conn[i]->stat != XK_REFRESHER_CONN_NEW) {
            close(rp->conn[i]->hp->fd);
            rp->conn[i]->stat = XK_REFRESHER_CONN_NEW;
        }
        
    clock_gettime(CLOCK_REALTIME, &rp->sts);
    ts_plus(&rp->rts, &rp->sts, XK_REFRESHER_REPORTINTERVAL);
    while (1) {
        /* check timeout */
        clock_gettime(CLOCK_REALTIME, &ts);
        if (dtsp && ts_less(dtsp, &ts))
            return -1; /* timeout */
        
        /* do report */
        if (ts_less(&rp->rts, &ts)) {
            
            printf("REPORT: tc=%d ec=%d hunger=%d lastsec=%d total=%d lastsec=%d\n",
                    rp->conn_used, rp->conn_err,
                    rp->hunger_total, rp->hunger_total - rp->hunger_last,
                    rp->recv_total, rp->recv_total - rp->recv_last);
            ts_plus(&rp->rts, &rp->rts, XK_REFRESHER_REPORTINTERVAL);
            rp->recv_last = rp->recv_total;
            rp->hunger_last = rp->hunger_total;
        }
        
        /* init http */
        for (i = 0; i < rp->conn_cnt; i++) {
            rcp = rp->conn[i]; hp = rcp->hp;
            if (rcp->stat == XK_REFRESHER_CONN_INITIAL) {
                fd = try_get_single_conn(rcp->pp);
                if (fd < 0) {
                    rp->hunger_total++;
                    continue;
                }
                set_tcp_nodelay(fd);
                set_ip_tos(fd);
                xk_http_init(hp, fd);
                rcp->recv_cnt = rcp->send_cnt = 0;
                rcp->stat = XK_REFRESHER_CONN_NORMAL;
                rcp->next_send = ts;
                rp->conn_used++;
                construct_request(rcp);
                init_response(hp);
            }
        }
        
        /* wait for r/w */
        FD_ZERO(&rset);
        FD_ZERO(&wset);
        tv.tv_sec = XK_REFRESHER_SELECTDELAY / 1000;
        tv.tv_usec = (XK_REFRESHER_SELECTDELAY % 1000) * 1000;
        maxfd = 0;
        for (i = 0; i < rp->conn_cnt; i++) {
            rcp = rp->conn[i]; fd = rcp->hp->fd;
            if (rcp->stat == XK_REFRESHER_CONN_NORMAL) {
                //printf("fd=%d\n", fd);
                FD_SET(fd, &rset);
                if ((rcp->send_cnt % XK_REFRESHER_REQUESTPRESEND != 0 ||
                     rcp->recv_cnt >= rcp->send_cnt) &&
                    rcp->send_cnt < XK_REFRESHER_REQUESTPRECONN &&
                    ts_less(&rcp->next_send, &ts))
                    FD_SET(fd, &wset);
                if (fd > maxfd) maxfd = fd;
            }
        }
        ret = select(maxfd + 1, &rset, &wset, NULL, &tv); /* block */
        if (ret < 0) err_sys("select error");
        
        /* recv */
        clock_gettime(CLOCK_REALTIME, &ts);
        for (i = 0; i < rp->conn_cnt; i++) {
            rcp = rp->conn[i]; hp = rcp->hp; fd = hp->fd;
            if (rcp->stat == XK_REFRESHER_CONN_NORMAL && FD_ISSET(fd, &rset)) {
                do {
                    if (hp->response_dataflag == 0) rcp->rts = ts;
                    while ((ret = recv_response(hp)) == 1)
                        if (!rcp->construct_flag && /* need two conditions */
                            rcp->recv_cnt >= rcp->construct_id) {
                            v = check_vacancy(rcp);
                            if (v >= 0 && judge(v))
                                goto done;
                        }
                    if (ret != 0) {
                        if (ret < 0) rcp->stat = XK_REFRESHER_CONN_ERROR;
                        break;
                    }
                    //puts("RECV");
                    //printf("r=%d s=%d\n", rcp->recv_cnt, rcp->send_cnt);
                    //puts("HEADER");nputs(hp->response->buf, hp->response->headerlen);
                    if (!rcp->construct_flag &&
                        rcp->recv_cnt >= rcp->construct_id &&
                        judge(v = check_vacancy(rcp)))
                        goto done;
                    rcp->recv_cnt++;
                    rp->recv_total++;
                    if (rcp->recv_cnt >= XK_REFRESHER_REQUESTPRECONN &&
                        hp->response_dataflag == 2) {
                        rcp->stat = XK_REFRESHER_CONN_CLEANUP;
                        break;
                    }
                    init_response(hp); /* set hp->response_dataflag to 0 */
                } while (rcp->recv_cnt < rcp->send_cnt);
                //printf("rcp->stat=%d\n", rcp->stat);
            }
        }
        
        /* send */
        for (i = 0; i < rp->conn_cnt; i++) {
            rcp = rp->conn[i]; hp = rcp->hp; fd = hp->fd;
            if (rcp->stat == XK_REFRESHER_CONN_NORMAL && FD_ISSET(fd, &wset)) {
                if (hp->request_dataflag == 0 &&
                     rcp->send_cnt % XK_REFRESHER_REQUESTPRESEND == 0)
                    ts_plus(&rcp->next_send, &ts, XK_REFRESHER_SENDDELAY);
                do {// msg_info("REAL_SEND");
                    while ((ret = send_request(hp)) == 1);
                    if (ret != 0) break;
                    //printf("s=%d\n", rcp->send_cnt);
                    rcp->send_cnt++;
                    construct_request(rcp);
                } while (rcp->send_cnt % XK_REFRESHER_REQUESTPRESEND != 0 &&
                         rcp->send_cnt < XK_REFRESHER_REQUESTPRECONN);
                if (ret < 0)
                    rcp->stat = XK_REFRESHER_CONN_ERROR;
            }
        }
        
        /* clean up */
        for (i = 0; i < rp->conn_cnt; i++) {
            rcp = rp->conn[i];
            if (rcp->stat == XK_REFRESHER_CONN_CLEANUP) {
                close(rcp->hp->fd);
                rcp->stat = XK_REFRESHER_CONN_INITIAL;
            }
            else if (rcp->stat == XK_REFRESHER_CONN_ERROR) {
                rp->conn_err++;
                close(rcp->hp->fd);
                rcp->stat = XK_REFRESHER_CONN_INITIAL;
            }
        }
    }
    /* shouldn't be here */

done: /* vacancy found */
    strlcpy(course_buf, rcp->course_code, buflen);
    if (rtsp) *rtsp = rcp->rts;
    return v;
}

