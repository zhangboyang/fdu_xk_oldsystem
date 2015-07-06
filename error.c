/*
 * from "Advanced Programming in the Unix Environment"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>      /* for definition of errno */
#include <stdarg.h>     /* ISO C variable aruments */
#include "xk.h"
#include "msg.h"
#include "error.h"

/*
 * Print a message and return to caller.
 * Caller specifies "errnoflag".
 */
static void err_doit(int errnoflag, int error, const char *fmt, va_list ap)
{
    char buf[XK_ERROR_MAXLINE];
    vsnprintf(buf, XK_ERROR_MAXLINE - 1, fmt, ap);
    if (errnoflag)
        snprintf(buf+strlen(buf), XK_ERROR_MAXLINE - strlen(buf) - 1, ": %s",
          strerror(error));
    strcat(buf, "\n");
    
    fflush(stdout);     /* in case stdout and stderr are the same */
    msg_flush();        /* flush message buffer */
    fputs(buf, stderr);
    fflush(NULL);       /* flushes all stdio output streams */
}

/*
 * Fatal error related to a system call.
 * Print a message and terminate.
 */
void err_sys(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    err_doit(1, errno, fmt, ap);
    va_end(ap);
    exit(1);
}

/*
 * Fatal error unrelated to a system call.
 * Print a message and terminate.
 */
void err_quit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    err_doit(0, 0, fmt, ap);
    va_end(ap);
    exit(1);
}

