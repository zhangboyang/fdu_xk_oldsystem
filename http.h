#ifndef XK_HTTP_H
#define XK_HTTP_H

#include "xk.h"

#define XK_HTTP_MAXLINE 4096
#define XK_HTTP_REQUESTBUF 4096
#define XK_HTTP_RESPONSEBUF 1048576
#define XK_HTTP_HOSTNAME "xk.fudan.edu.cn"
#define XK_HTTP_USERAGENT "helloworld"

typedef struct {
    char *buf;
    int len;
    int sendlen;
    int is_post;
} xk_http_request;

typedef struct {
    char *buf;
    char *data;     /* pointer to data, should be 'buf + datalen' */
    int chunked;    /* < 0 unknown, == 0 not chunked, > 0 chunked */
    int lastchunk;  /* == 0 not last chunk, != 0 last chunk */
    int chunkstat;  /* chunk parser status
                     *  > 0 how many bytes in chunk that haven't reveived
                     *  == 0 next chunk header not fully received */
    char *chunkptr; /* pointer to chunk header, may be invalid after memmove */
    int stat;       /* http status code, > 0 means header have been received */
    int flag;       /* response parser status
                     *  > 0   need continue
                     *  == 0  completed
                     *  < 0   error occured */
    int headerlen;  /* http header length */
    int datalen;    /* response data length, set to -1 if method is HEAD */
    int contentlen; /* content length */
    int recvlen;    /* bytes received */
    int dataflag;   /* id of no data response, -1 means no such response */
    char *nextdata; /* pointer to data of next response */
    int nextlen;    /* length of nextdata */
} xk_http_response;


typedef struct {
    xk_http_request *request;
    xk_http_response *response;
    int fd;
    int request_cnt;
    int response_cnt;
    
    /* dataflag:  == 0: initialized but havn't called send yet
     *            == 1: data transmitting
     *            == 2: data transmit finished */
    int request_dataflag;
    int response_dataflag;
    
    int fvhint;      /* hint value for speed up find_vacancy */
} xk_http;

/* useful functions */
#define http_data(hp) ((hp)->response->data)
#define http_datalen(hp) ((hp)->response->datalen)
int find_http_header(const char *buf, int buflen);

/* alloc and free */
void xk_http_init(xk_http *hp, int fd);
xk_http *xk_http_new(void);
void xk_http_free(xk_http *hp);

/* response */
int get_header(xk_http *hp, const char *fmt, ...);
int recv_response(xk_http *hp);
void init_response(xk_http *hp);

/* request */
int send_request(xk_http *hp);
void add_header(xk_http *hp, const char *fmt, ...);
void finalize_request(xk_http *hp, ...);
void init_request(xk_http *hp, const char *method, const char *urifmt, ...);
void reuse_request(xk_http *hp);

#endif
