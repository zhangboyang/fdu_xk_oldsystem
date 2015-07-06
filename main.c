#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "xk.h"
#include "addr.h"
#include "pool.h"
#include "sleep.h"
#include "msg.h"
#include "fduxk.h"
#include "fflag.h"
#include "tmoffset.h"
#include "refresher.h"

#define MAXPOOL XK_ADDR_MAXADDRINFO
#define MAXLINE 40960


void read_fd(int fd)
{
    char c;
    while (recv(fd, &c, 1, 0) == 1) putchar(c);
}
void nputs(char *p, int c) { while (c--) putchar(*p++); }
int simple_hash(char *p, int c)
{
    int s = 0;
    while (c--) s += *p++;
    return s;
}


/*time_t ask_time(void *p) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return rand() > RAND_MAX / 2 ? ts.tv_sec : -1; }

void my_main()
{
    printf("offset=%d\n", time_offset(ask_time, NULL));
    exit(0);
}
*/
/*int get_response(int fd)
{
    char buf[MAXLINE];
    int ret;
    while ((ret = recv(fd, buf, sizeof(buf), 0)) < 0);
    buf[ret] = '\0';
    msg_dbg("RESPONSE: %s", buf);
    get_response(fd);
}*/

/*time_t ask_remote_time(void *ptr)
{
    struct tm rtm;
    xk_http *hp = ptr;
    xk_http_response *sp = hp->response;
    char rdatestr[XK_HTTP_MAXLINE];
    init_request(hp, "HEAD", "/");
    finalize_request(hp);
    while (send_request(hp) > 0);
    init_response(hp);
    while (recv_response(hp) > 0);
    //printf("SC=%d HL=%d DL=%d\n", sp->stat, sp->headerlen, sp->datalen);
    printf("HEADER\n"); nputs(sp->buf, sp->headerlen);
//    printf("DATA\n"); nputs(sp->data, sp->datalen);
//    printf("END\n");
    get_header(hp, "Date: %[^\r]", rdatestr);
    strptime(rdatestr, "%a, %d %b %Y %T GMT", &rtm);
    printf("time=%d mktime=%d\n", (int) time(NULL), (int) mktime(&rtm));
    return mktime(&rtm);
}*/

void xk_offset(int fd)
{
//    printf("offset=%d\n", (int) time_offset(ask_remote_time, xk_http_new(fd)));
}


/*
void hahaha(xk_pool *pp, const char *cookie)
{
    struct timespec ts;
    const char *cc = "INFO119004.01";
    xk_http *hp;
    xk_http_response *sp;
    int i, fd;
    int v, tv, rv;
    fd_set rset, wset;
    while (1) {
        fd = get_single_conn(pp);
        set_tcp_nodelay(fd);
        set_ip_tos(fd);
        hp = xk_http_new();
        xk_http_init(hp, fd);
        set_block(fd);
        sp = hp->response;
        for (i = 1; i <= 80; i++) {
            
            init_request(hp, "GET", "/xk/sekcoursepeos.jsp?xkh=%s", cc);
            add_header(hp, "Cookie: JSESSIONID=%s", cookie);
            finalize_request(hp);
            clock_gettime(CLOCK_REALTIME, &ts);
            printf("SEND-TS: %d.%09d\n", (int) ts.tv_sec, (int) ts.tv_nsec);
            //FD_ZERO(&wset); FD_SET(fd, &wset);
            //do select(fd + 1, NULL, &wset, NULL, NULL);
            while (send_request(hp) > 0);
            
            init_response(hp);
            //FD_ZERO(&rset); FD_SET(fd, &rset);
            //do select(fd + 1, &rset, NULL, NULL, NULL);
            while (recv_response(hp) > 0);
            clock_gettime(CLOCK_REALTIME, &ts);
            printf("RECV-TS: %d.%09d\n", (int) ts.tv_sec, (int) ts.tv_nsec);
            printf("SC=%d HL=%d DL=%d\n", sp->stat, sp->headerlen, sp->datalen);
            printf("HEADER\n"); nputs(sp->buf, sp->headerlen);
//            printf("DATA\n"); nputs(sp->data + 2700, 500);
            printf("END\n");
            v = find_vacancy(hp, cc, &tv, &rv);
            printf("V=%d,%d,%d\n", v, tv, rv);
            exit(0);
        }
        close(hp->fd);
        xk_http_free(hp);
    }
    
}*/


// USER INFORMATION - START
#define MAXCONN 10
#define REFRESHER_SOLT_USE 5
#define XKSRV_ADDR1 "61.129.42.49"
#define XKSRV_ADDR2 "61.129.42.49" // .122 is dead currently
const char * const XK_FDUXK_USERNAME = "your_student_id";
const char * const XK_FDUXK_PASSWORD = "your_password";
const char * const ccode = "FORE110044.04";
int my_judge1(int x) { return x > 0; }
// USER INFORMATION - START

struct addrinfo *pool_addr[128];
int pool_addr_cnt;

int main()
{
    xk_pool *pp[MAXPOOL];
    char buf[MAXLINE];
    int addr_cnt;
    int i, j;

    /* set timezone to UTC, for gmtime() to parse http date header */
    setenv("TZ", "", 1);
    tzset();

    /* ignore SIGPIPE or the program will terminate unexpectedly */
    signal(SIGPIPE, SIG_IGN);
    
    /* create the message flushing thread */
    create_msg_flush_thread();
    
    /* query server addresses */
    query_addr(XKSRV_ADDR1, "http");
    query_addr(XKSRV_ADDR2, "http");
    addr_cnt = get_addr_cnt();
    for (i = 0; i < addr_cnt; i++) {
        straddr(get_addr(i), buf, sizeof(buf));
        // ugly patch start
        pool_addr[i] = get_addr(i);
        pool_addr_cnt = addr_cnt;
        // ugly patch end
        msg_info("server addr: %s", buf);
    }
    
    /* create connection pool */
    for (i = 0; i < addr_cnt; i++) {
        pp[i] = create_pool(MAXCONN, get_addr(i)); // addr is no use here, because of that ugly patch
        msg_info("start connection pool %d", i);
    }
    
    /* wait for connect */
    for (i = 0; i < 1; i++) {
        msg_info("waiting for pool %d ...", i);
        for (j = 1; j < MAXCONN; j++) {
            wait_pool(pp[i], j);
            pool_status(pp[i], buf, sizeof(buf));
            msg_info("pool %d status: %s", i, buf);
        }
    }
    

    char course_buf[1000];
    const char *course_code = ccode;
    int v;
    int ret;
    xk_fduxksession *fsp;
    //while (1) printf("fd=%d\n", get_single_conn(pp[0]));
    //fd1 = get_single_conn(pp[0]);
    //xk_offset(fd1);
    //get_conn(pp[1], &fd2, 1);
    while (!((fsp = xk_prepare_login(pp[0])) && xk_login(pp[0], fsp))) {
        msg_err("login failed, reason: %s", fsp->errmsg);
    }
    msg_notice("cookie=%s\n", fsp->jsessionid);
    ret = xk_prepare_select(pp[0], fsp);
    sleep_ms(3000);
    msg_notice("ret=%d token=%d, ocr=%.4s", ret, fsp->token, fsp->ocr_result);
    //hahaha(pp[0], cookie); // simple
    xk_refresher *rp = new_refresher(REFRESHER_SOLT_USE + 10);
    for (i = 1; i <= REFRESHER_SOLT_USE; i++) {
    add_target(rp, pp[0], fsp->jsessionid, course_code);
    }
    
    //wait_pool(pp[0], MAXCONN);
    msg_info("run_refresher");
    v = run_refresher(rp, my_judge1, course_buf, sizeof(course_buf), NULL, NULL);
    msg_info("return value = %s, v = %d\n", course_buf, v);
    
    ret = xk_select(pp[0], fsp, course_buf);
    if (!ret) msg_err("ret=%d errmsg=%s\n", ret, fsp->errmsg);
    else msg_good("ret=%d errmsg=%s\n", ret, fsp->errmsg);
/*    reuse_refresher(rp);
    for (i = 1; i <= 1; i++) {
    add_target(rp, NULL, NULL, "XDSY118002.01");
    }
    msg_info("run_refresher");
    v = run_refresher(rp, my_judge1, course_buf, sizeof(course_buf), NULL, NULL);
    msg_good("return value = %s, v = %d", course_buf, v);*/
    
    /* easter egg: from the movie Tron */
    msg_info("end of line");
    
    /* cancel all pool */
    for (i = 0; i < addr_cnt; i++)
        cancel_pool(pp[i]);
    
    /* free addrinfo */
    free_addr();
    
    /* wait and cancel the message flushing thread */
    cancel_msg_flush_thread();
    
    return 0;
}
