#ifndef XK_FDUXK_H
#define XK_FDUXK_H

#include "xk.h"
#include "strlcpy.h"

#define XK_FDUXK_MAXJSESSIONIDLEN 128
#define XK_FDUXK_MAXERRMSGLEN 4096

//FIXME
#define recycle_conn(a, b) close(b)

typedef struct {
    char jsessionid[XK_FDUXK_MAXJSESSIONIDLEN];
    int token;
    char ocr_result[4];
    char errmsg[XK_FDUXK_MAXERRMSGLEN];
} xk_fduxksession;
#define fs_seterror(fsp, str) \
    strlcpy((fsp)->errmsg, (str), XK_FDUXK_MAXERRMSGLEN);
#define fs_setjsessionid(fsp, str) \
    strlcpy((fsp)->jsessionid, (str), XK_FDUXK_MAXJSESSIONIDLEN);

int find_alert(xk_http *hp, char *buf, int buflen);
int find_token(xk_http *hp);
int find_vacancy(xk_http *hp, const char *course_code, int *tp, int *rp);

xk_fduxksession *xk_prepare_login(xk_pool *pp);
int xk_login(xk_pool *pp, xk_fduxksession *fsp);
int xk_prepare_select(xk_pool *pp, xk_fduxksession *fsp);
int xk_select(xk_pool *pp, xk_fduxksession *fsp, const char *course_code);

#endif
