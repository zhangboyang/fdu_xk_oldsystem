#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>      /* errno */
#include <sys/select.h> /* select */
#include <sys/socket.h> /* send */

#include "xk.h"
#include "error.h"
#include "memstr.h"
#include "http.h"

/* useful functions */
static int min(int a, int b)
{
    return a < b ? a : b;
}

int find_http_header(const char *buf, int buflen)
{
    /* find and return http header length, if not found return -1 */
    const char *magic = "\r\n\r\n";
    char *ptr = memstr(buf, magic, buflen);
    return ptr ? ptr - buf + strlen(magic) : -1;
}

/* alloc and free */
static xk_http *xk_http_malloc()
{
    xk_http *ret;
    if ((ret = malloc(sizeof(xk_http))) == NULL)
        err_sys("malloc error");
    if ((ret->request = malloc(sizeof(xk_http_request))) == NULL)
        err_sys("malloc error");
    if ((ret->response = malloc(sizeof(xk_http_response))) == NULL)
        err_sys("malloc error");
    if ((ret->request->buf = malloc(XK_HTTP_REQUESTBUF)) == NULL)
        err_sys("malloc error");
    if ((ret->response->buf = malloc(XK_HTTP_RESPONSEBUF)) == NULL)
        err_sys("malloc error");
    return ret;
}

void xk_http_init(xk_http *hp, int fd)
{
    hp->response->nextlen = 0;
    hp->response->dataflag = -1;
    hp->fd = fd;
    hp->request_cnt = 0;
    hp->response_cnt = 0;
}

xk_http *xk_http_new(void)
{
    xk_http *ret = xk_http_malloc();
    return ret;
}

void xk_http_free(xk_http *hp)
{
    free(hp->request->buf);
    free(hp->response->buf);
    free(hp->request);
    free(hp->response);
    free(hp);
}

/* parser */
static int parse_chunk_header(const char *buf, int len, int *dlen, int *hlen)
{
    const char *p;
    for (p = buf; p - buf < len && isxdigit(*p); p++);
    if (p - buf + 2 > len) return 1;
    if (*p != '\r' || *(p + 1) != '\n') return -1;
    *hlen = p - buf + 2;
    sscanf(buf, "%x", dlen);
    return 0;
}

static int parse_response(xk_http *hp)
{
    xk_http_response *ptr = hp->response;
    int len, ret;
    int clen, hlen, dlen;
    char buf[XK_HTTP_MAXLINE];
    
    /* check if we have finished */
    if (ptr->flag == 0)
        return 0;
    
    /* check if we have full header */
    if (!ptr->data) {
        len = find_http_header(ptr->buf, ptr->recvlen);
        if (len < 0) return ptr->flag = 1; /* haven't received header yet */
        ptr->data = ptr->buf + len;
        ptr->headerlen = len;
        ptr->chunked = -1;
        ptr->chunkptr = ptr->data;
        ptr->chunkstat = 0;
        ptr->lastchunk = 0;
        ptr->flag = 1;
        ptr->datalen = 0;
    }
    
    /* get response status code */
    ret = get_header(hp, "HTTP/1.1 %d", &ptr->stat);
    if (ret != 1) return ptr->flag = -1;
    
    /* check data format */
    if (ptr->chunked < 0) {
        if (ptr->dataflag == hp->response_cnt) {
            ptr->dataflag = -1;
            ptr->nextdata = ptr->data;
            ptr->nextlen = ptr->recvlen - ptr->headerlen;
            return ptr->flag = 0; /* check dataflag */
        }
        ret = get_header(hp, "Transfer-Encoding: %s", buf);
        if (ret == 1 && strcmp(buf, "chunked") == 0) {
            ptr->chunked = 1;
        } else {
            ret = get_header(hp, "Content-Length: %d", &ptr->contentlen);
            if (ret != 1) ptr->contentlen = 0; /* assume no data */
            ptr->chunked = 0;
        }
    }
    
    /* parse data */
    if (ptr->chunked > 0) {
        /* data is chunked */
        if (ptr->chunkstat > 0) {
            clen = ptr->buf + ptr->recvlen - ptr->chunkptr;
            len = min(clen, ptr->chunkstat);
            memmove(ptr->data + ptr->datalen, ptr->chunkptr, len);
            ptr->datalen += len;
            ptr->chunkptr += len;
            ptr->chunkstat -= len;
            if (ptr->chunkstat > 0) return ptr->flag = 1;
            else ptr->datalen -= 2;
        }
        while (!ptr->lastchunk) {
            clen = ptr->buf + ptr->recvlen - ptr->chunkptr;
            ret = parse_chunk_header(ptr->chunkptr, clen, &dlen, &hlen);
            if (ret != 0) return ptr->flag = ret;
            if (dlen == 0) ptr->lastchunk = 1;
            dlen += 2, clen -= hlen;
            len = min(clen, dlen);
            ptr->chunkptr += hlen;
            memmove(ptr->data + ptr->datalen, ptr->chunkptr, len);
            ptr->datalen += len;
            ptr->chunkptr += len;
            ptr->chunkstat = dlen - len;
            if (ptr->chunkstat)
                return ptr->flag = 1;
            ptr->datalen -= 2;
        }
        /* process data of next response */
        ptr->nextdata = ptr->chunkptr;
        ptr->nextlen = ptr->buf + ptr->recvlen - ptr->chunkptr;
        return ptr->flag = 0;
    } else {
        /* ptr->datalen = min(ptr->recvlen - ptr->headerlen, ptr->contentlen) */
        if (ptr->recvlen - ptr->headerlen < ptr->contentlen) {
            /* data haven't received completely */
            ptr->datalen = ptr->recvlen - ptr->headerlen;
            return ptr->flag = 1;
        } else {
            /* data received completely */
            ptr->datalen = ptr->contentlen;
            /* process data of next response */
            ptr->nextdata = ptr->data + ptr->datalen;
            ptr->nextlen = ptr->recvlen - ptr->headerlen - ptr->datalen;
            return ptr->flag = 0;
        }
    }
}


/* response */
int get_header(xk_http *hp, const char *fmt, ...)
{
    /* %s string length is guaranteed less than XK_HTTP_MAXLINE */
    xk_http_response *ptr = hp->response;
    int ret;
    char *lp, *np;
    va_list ap, tp;
    
    va_start(ap, fmt);
    ret = 0;
    for (lp = ptr->buf; *lp != '\r' && ret == 0; lp = np) {
        va_copy(tp, ap);
        np = strchr(lp, '\n') + 1;
        ret = np - lp < XK_HTTP_MAXLINE ? vsscanf(lp, fmt, tp) : -1;
        va_end(tp);
    }
    va_end(ap);
    
    return ret;
}

int recv_response(xk_http *hp)
{
    /* return value == 2  need continue, especially recv would block
     * return value == 1  need continue
     * return value == 0  completed
     * return value < 0   error occured */
    xk_http_response *ptr = hp->response;
    int ret;
    char *buf = ptr->buf + ptr->recvlen;
    int len = XK_HTTP_RESPONSEBUF - ptr->recvlen;
    /* hp->response_dataflag == 0 && ptr->recvlen > 0
     * means there is data received by last response, should parse it first
     * i.e. if not (...) should do real recv, otherwise should just parse */
    if (hp->response_dataflag != 0 || ptr->recvlen == 0) { /* if not (...) */
        /* do real recv */
        ret = recv(hp->fd, buf, len, 0);
        hp->response_dataflag = 1;
        //printf("recv(%d,%p,%d,0)=%d (errno=%s)\n", hp->fd, buf, len, ret, strerror(errno));
        if (ret == 0 || (ret < 0 && errno != EAGAIN)) return -1;
        else if (ret < 0) return 2;
        ptr->recvlen += ret;
    }
    hp->response_dataflag = 1;
    ret = parse_response(hp); /* should only returns -1 or 0 or 1 */
    if (ret == 0) hp->response_dataflag = 2;
    return ret;
}

void init_response(xk_http *hp)
{
    xk_http_response *ptr = hp->response;
    hp->response_cnt++;
    ptr->data = NULL;
    ptr->stat = 0;
    ptr->flag = 1;
    hp->fvhint = 0;
    hp->response_dataflag = 0;
    /* process pre-received data */
    if ((ptr->recvlen = ptr->nextlen) > 0)
        memmove(ptr->buf, ptr->nextdata, ptr->nextlen);
}

/* request */
int send_request(xk_http *hp)
{
    /* return value == 2  need continue, especially send would block
     * return value == 1  need continue
     * return value == 0  completed
     * return value < 0   error occured */
    xk_http_request *ptr = hp->request;
    int ret;
    char *buf = ptr->buf + ptr->sendlen;
    int len = ptr->len - ptr->sendlen;
    hp->request_dataflag = 1;
    ret = send(hp->fd, buf, len, 0);
    //printf("send(%d,%p,%d,0)=%d\n", hp->fd, buf, len, ret);
    if (ret < 0 && errno != EAGAIN) return -1;
    else if (ret < 0) return 2;
    ptr->sendlen += ret;
    if (ptr->len > ptr->sendlen) return 1;
    else {
        hp->request_dataflag = 2;
        return 0;
    }
}

void add_header(xk_http *hp, const char *fmt, ...)
{
    xk_http_request *ptr = hp->request;
    char *buf;
    int len, ret;
    va_list ap;
    
    va_start(ap, fmt);
    buf = ptr->buf + ptr->len;
    len = XK_HTTP_REQUESTBUF - ptr->len;
    if (len < 2) err_quit("request header buffer too small");
    ret = vsnprintf(buf, len - 1, fmt, ap);
    if (ret < 0) err_sys("vsnprintf error");
    buf[ret] = '\r';
    buf[ret + 1] = '\n';
    ptr->len += ret + 2;
    va_end(ap);
}

void finalize_request(xk_http *hp, ...)
{
    xk_http_request *ptr = hp->request;
    char buf[XK_HTTP_MAXLINE];
    const char *fmt;
    int len;
    va_list ap;
    va_start(ap, hp);
    
    if (!ptr->is_post) {
        /* no addtional data */
        add_header(hp, "");
    } else {
        /* method is POST */
        fmt = va_arg(ap, const char *);
        len = vsnprintf(buf, sizeof(buf), fmt, ap);
        if (len < 0)
            err_sys("vsnprintf error");
        if (len + 1 >= sizeof(buf))
            err_quit("request header buffer too small");
        add_header(hp, "Content-Length: %d", len);
        add_header(hp, "Content-Type: application/x-www-form-urlencoded");
        add_header(hp, "");
        if (len >= XK_HTTP_REQUESTBUF - ptr->len)
            err_quit("request header buffer too small");
        memcpy(ptr->buf + ptr->len, buf, len);
        ptr->len += len;
    }
    
    va_end(ap);
}

void init_request(xk_http *hp, const char *method, const char *urifmt, ...)
{
    xk_http_request *ptr = hp->request;
    int len;
    char buf[XK_HTTP_MAXLINE];
    va_list ap;
    
    va_start(ap, urifmt);
    len = vsnprintf(buf, sizeof(buf), urifmt, ap);
    if (len < 0)
        err_sys("vsnprintf error");
    if (len + 1 >= sizeof(buf))
        err_quit("request header buffer too small");
    
    hp->request_cnt++;
    ptr->len = 0;
    ptr->sendlen = 0;
    ptr->is_post = strcmp(method, "POST") == 0;
    hp->request_dataflag = 0;
    if (strcmp(method, "HEAD") == 0) {
        if (hp->response->dataflag >= 0)
            err_quit("too many HEAD request");
        hp->response->dataflag = hp->request_cnt;
    }
    
    add_header(hp, "%s %s HTTP/1.1", method, buf);
    add_header(hp, "Host: %s", XK_HTTP_HOSTNAME);
    add_header(hp, "User-Agent: %s", XK_HTTP_USERAGENT);
    add_header(hp, "Connection: keep-alive");
    
    va_end(ap);
}

void reuse_request(xk_http *hp)
{
    xk_http_request *ptr = hp->request;
    if (ptr->sendlen) hp->request_cnt++;
    ptr->sendlen = 0;
    hp->request_dataflag = 0;
}

