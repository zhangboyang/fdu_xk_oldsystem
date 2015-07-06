#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>     /* close */
#include "xk.h"
#include "http.h"
#include "memstr.h"
#include "pool.h"
#include "fflag.h"
#include "ocr.h"
#include "fduxk.h"
#include "error.h"
#include "strlcpy.h"

extern const char * const XK_FDUXK_USERNAME;
extern const char * const XK_FDUXK_PASSWORD;
static int max(int a, int b)
{
    return a > b ? a : b;
}

static int min(int a, int b)
{
    return a < b ? a : b;
}

int find_alert(xk_http *hp, char *buf, int buflen)
{
    /* return value: >= 0 the length of alert string, < 0 not found yet */
    char *data;
    int datalen;
    char *p, *q, *r;
    int len;
    
    if (!http_data(hp)) return -1; /* there is no data received yet */
    data = http_data(hp);
    datalen = http_datalen(hp);
    
    if ((p = memstr(data, "start_alert()", datalen)) == NULL)
        return -1;
    len = datalen - (p - data);
    if (len <= 0 || (q = memchr(p, '\"', len)) == NULL)
        return -1;
    q++; len = datalen - (q - data);
    if (len <= 0 || (r = memchr(q, '\"', len)) == NULL)
        return -1;
    
    len = min(r - q, buflen - 1);
    memcpy(buf, q, len);
    buf[len] = '\0';
    return len;
}

int find_token(xk_http *hp)
{
    /* return value: >= 0 token value, < 0 not found yet */
    char *data;
    int datalen;
    const char *magic = "<input type=\"hidden\" name=\"token\" value=\"";
    char *p, *q;
    int len, ret, token;
    
    if (!http_data(hp)) return -1; /* there is no data received yet */
    data = http_data(hp);
    datalen = http_datalen(hp);
    
    if ((p = memstr(data, magic, datalen)) == NULL)
        return -1;
    p += strlen(magic); len = datalen - (p - data);
    if (len <= 0 || (q = memchr(p, '\"', len)) == NULL)
        return -1;
    if (q - p > 4) return -1; /* token should be in range [0, 9999] */
    ret = sscanf(p, "%d", &token);
    return ret == 1 ? token : -1;
}

int find_vacancy(xk_http *hp, const char *course_code, int *tp, int *rp)
{
    /* hint parameter: first hint bytes are not checked
     * return value: >= 0 true vacancy value, < 0 not found yet
     * if tp != NULL, total vacancy is stored in tp
     * if rp != NULL, reserved vacancy is stored in rp */
    char *data;
    int datalen;
    const char *magic = "<span class=\"style1\"> ";
    char *p, *q;
    int i, len, t, r;
    
    if (!http_data(hp)) return -1; /* there is no data received yet */
    data = http_data(hp) + hp->fvhint;
    datalen = http_datalen(hp) - hp->fvhint;
    
    if ((p = memstr(data, course_code, datalen)) == NULL) {
        hp->fvhint += max(datalen - strlen(course_code), 0);
        return -1;
    }
    p += strlen(magic); len = datalen - (p - data);
    
    for (i = 0; i < 3; i++) {
        /* jump to next td */
        if (len <= 0 || (p = memstr(p, magic, len)) == NULL) return -1;
        p += strlen(magic); len = datalen - (p - data);
    }
    
    /* assume that vacancy should be in range [0, 9999] (4 digits) */
    for (q = p; q - p < 4 && q - p < len && isdigit(*q); q++);
    if (isdigit(*q)) return -1; /* not enough data, or too many digits */
    if (sscanf(p, "%d", &t) != 1) return -1;
    
    /* jump to next td */
    if (len <= 0 || (p = memstr(p, magic, len)) == NULL) return -1;
    p += strlen(magic); len = datalen - (p - data);
    
    /* assume that vacancy should be in range [0, 9999] (4 digits) */
    for (q = p; q - p < 4 && q - p < len && isdigit(*q); q++);
    if (isdigit(*q)) return -1; /* not enough data, or too many digits */
    if (sscanf(p, "%d", &r) != 1) return -1;
    
    if (tp) *tp = t;
    if (rp) *rp = r;
    return t - r; /* t - r is the true vacancy value */ 
}




xk_fduxksession *xk_prepare_login(xk_pool *pp)
{
    /* try login to xk system
     * return value: != NULL success, == NULL failed */
    int fd, ret;
    char buf[XK_HTTP_MAXLINE];
    xk_http *hp;
    xk_fduxksession *fsp;
    
    /* alloc memory */
    if ((fsp = malloc(sizeof(xk_fduxksession))) == NULL)
        err_sys("malloc error");
    
    /* get socket */
    fd = get_single_conn(pp);
    set_block(fd);
    set_tcp_nodelay(fd);
    set_ip_tos(fd);
    
    /* new http handler */
    hp = xk_http_new();
    xk_http_init(hp, fd);
    
    /* get captcha image */
    init_request(hp, "GET", "/xk/image.do");
    finalize_request(hp);
    while ((ret = send_request(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "send_request error"); goto fail; }

    init_response(hp);
    while ((ret = recv_response(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "recv_response error"); goto fail; }
    
    /* ocr the captcha */
    ret = ocr_jpeg(http_data(hp), http_datalen(hp), fsp->ocr_result);
    if (ret < 0) { fs_seterror(fsp, "ocr_jpeg failed"); goto fail; }
    
    /* parse cookie */
    ret = get_header(hp, "Set-Cookie: JSESSIONID=%[^;]", buf);
    if (ret != 1) { fs_seterror(fsp, "parse cookie failed"); goto fail; }
    
    /* success */
    fs_setjsessionid(fsp, buf);
    
    recycle_conn(pp, hp->fd);
    xk_http_free(hp);
    return fsp;

fail: /* process error */
    return NULL;
}
int xk_login(xk_pool *pp, xk_fduxksession *fsp)
{
    /* try login to xk system
     * return value: != 0 success, == 0 failed */
    
    int fd, ret;
    xk_http *hp;
    
    /* get socket */
    fd = get_single_conn(pp);
    set_block(fd);
    set_tcp_nodelay(fd);
    set_ip_tos(fd);
    
    /* new http handler */
    hp = xk_http_new();
    xk_http_init(hp, fd);
    
    /* try login */
    init_request(hp, "POST", "/xk/loginServlet");
    add_header(hp, "Cookie: JSESSIONID=%s", fsp->jsessionid);
    finalize_request(hp, "studentId=%s&password=%s&rand=%s"
                         "&Submit2=%%E6%%8F%%90%%E4%%BA%%A4",
                          XK_FDUXK_USERNAME, XK_FDUXK_PASSWORD,
                          fsp->ocr_result);
    while ((ret = send_request(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "send_request error"); goto fail; }
    
    init_response(hp);
    while ((ret = recv_response(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "recv_response error"); goto fail; }
    
    /* parse response */
    ret = find_alert(hp, fsp->errmsg, XK_FDUXK_MAXERRMSGLEN);
    if (ret >= 0) ret = 0; /* found alert means login failed */
    else ret = 1;
    
    recycle_conn(pp, hp->fd);
    
done: /* cleanup and return */
    xk_http_free(hp);
    return ret;

fail: /* process error */
    close(hp->fd);
    ret = 0; /* set return value to fail */
    goto done;
}

int xk_prepare_select(xk_pool *pp, xk_fduxksession *fsp)
{
    /* try to get token and captcha, prepare for select
     * return value: != 0 success, == 0 failed */
    
    int fd, ret;
    xk_http *hp;
    
    /* get socket */
    fd = get_single_conn(pp);
    set_block(fd);
    set_tcp_nodelay(fd);
    set_ip_tos(fd);
    
    /* new http handler */
    hp = xk_http_new();
    xk_http_init(hp, fd);
    
    /* first: get token */
    init_request(hp, "GET", "/xk/input.jsp");
    add_header(hp, "Cookie: JSESSIONID=%s", fsp->jsessionid);
    finalize_request(hp);
    while ((ret = send_request(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "send_request error"); goto fail; }
    
    init_response(hp);
    while ((ret = recv_response(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "recv_response error"); goto fail; }
    fsp->token = find_token(hp); /* FIXME: need optimize */
    if (fsp->token < 0) { fs_seterror(fsp, "find_token failed"); goto fail; }
    
    /* second: get captcha */
    init_request(hp, "GET", "/xk/image.do?token=%d", fsp->token);
    add_header(hp, "Cookie: JSESSIONID=%s", fsp->jsessionid);
    finalize_request(hp);
    while ((ret = send_request(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "send_request error"); goto fail; }

    init_response(hp);
    while ((ret = recv_response(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "recv_response error"); goto fail; }
    
    /* ocr captcha */
    ret = ocr_jpeg(http_data(hp), http_datalen(hp), fsp->ocr_result);
    if (ret < 0) { fs_seterror(fsp, "ocr_jpeg failed"); goto fail; }
    
    /* success */
    ret = 1;
    recycle_conn(pp, hp->fd);

done: /* cleanup and return */
    xk_http_free(hp);
    return ret;

fail: /* process error */
    close(hp->fd);
    ret = 0; /* set return value to fail */
    goto done;
}

int xk_select(xk_pool *pp, xk_fduxksession *fsp, const char *course_code)
{
    /* try to select a course
     * return value: != 0 success, == 0 failed */
    
    int fd, ret;
    const char *magic1 = "Course added";
    const char *magic2 = "\xe9\x80\x89\xe8\xaf\xbe\xe6\x88\x90\xe5\x8a\x9f";
    xk_http *hp;
    
    /* get socket */
    fd = get_single_conn(pp);
    set_block(fd);
    set_tcp_nodelay(fd);
    set_ip_tos(fd);
    
    /* new http handler */
    hp = xk_http_new();
    xk_http_init(hp, fd);
    
    /* try login */
    init_request(hp, "POST", "/xk/doSelectServlet");
    add_header(hp, "Cookie: JSESSIONID=%s", fsp->jsessionid);
    finalize_request(hp, "token=%d&selectionId=%s&xklb=ss&rand=%.4s",
                          fsp->token, course_code, fsp->ocr_result);
    while ((ret = send_request(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "send_request error"); goto fail; }
    
    init_response(hp);
    while ((ret = recv_response(hp)) > 0);
    if (ret < 0) { fs_seterror(fsp, "recv_response error"); goto fail; }
    
    /* parse response */
    ret = find_alert(hp, fsp->errmsg, XK_FDUXK_MAXERRMSGLEN);
    if (ret < 0 || (strcmp(fsp->errmsg, magic1) != 0 &&
                    strcmp(fsp->errmsg, magic2) != 0 )) {
        ret = 0; /* have no alert or alert doesn't match magic means failed */
    } else {
        ret = 1; /* have alert and alert match magin means succeed */
    }
    
    recycle_conn(pp, hp->fd);

done: /* cleanup and return */
    xk_http_free(hp);
    return ret;

fail: /* process error */
    close(hp->fd);
    ret = 0; /* set return value to fail */
    goto done;
}
