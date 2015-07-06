#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include "xk.h"
#include "error.h"
#include "sleep.h"
#include "msg.h"

static volatile int cancel;
static pthread_t tid;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static xk_msg *head = NULL, *tail = NULL; /* protected by mutex */

static void real_output_msg(const xk_msg *ptr)
{
/*
    XK_MSG_DEBUG,
    XK_MSG_INFO,
    XK_MSG_NOTICE
    XK_MSG_GOOD,
    XK_MSG_BAD,
    XK_MSG_ERROR
*/
    const char *head[] = {"\033[0m", "\033[0m", "\033[34m", "\033[32m", "\033[31m", "\033[31m"};
    printf("%s[%10d.%09d][%d] %s%s\n", head[ptr->type], (int) ptr->ts.tv_sec, (int) ptr->ts.tv_nsec, ptr->type, ptr->str, "\033[0m");
}

static void *msg_flush_thread(void *arg)
{
    while (1) {
        msg_flush();
        if (cancel) break;
        sleep_ms(XK_MSG_FLUSHINTERVAL);
    }
    return (void *) 0;
}

static void msg_doit(xk_msg_type type, const char *fmt, va_list ap)
{
    xk_msg *ptr;
    char buf[XK_MSG_MAXLINE];
    int len;
    
    /* construct the message string */
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len < 0) err_sys("vsnprintf error");
    
    /* construct list node */
    if ((ptr = malloc(sizeof(xk_msg))) == NULL)
        err_sys("malloc error");
    if ((ptr->str = malloc(len + 1)) == NULL)
        err_sys("malloc error");
    strcpy(ptr->str, buf); /* buffer have enough space */
    ptr->type = type;
    clock_gettime(CLOCK_REALTIME, &ptr->ts);
    ptr->next = NULL;
    
    /* insert to the linked list */
    pthread_mutex_lock(&mutex); /* enter critical section */
    if (tail != NULL)
        tail->next = ptr, tail = ptr;
    else
        head = tail = ptr;
    pthread_mutex_unlock(&mutex); /* leave critical section */
}

void msg_flush()
{
    xk_msg *ptr, *next;
    
    /* take out old linked list */
    pthread_mutex_lock(&mutex); /* enter critical section */
    ptr = head;
    head = tail = NULL;
    pthread_mutex_unlock(&mutex); /* leave critical section */
    
    while (ptr != NULL) {
        real_output_msg(ptr);
        next = ptr->next;
        free(ptr->str);
        free(ptr);
        ptr = next;
    }
}

void create_msg_flush_thread()
{
    cancel = 0;
    if (pthread_create(&tid, NULL, msg_flush_thread, NULL) != 0)
        err_quit("pthread_create error");
}

void cancel_msg_flush_thread()
{
    cancel = 1;
    pthread_join(tid, NULL);
}

void msg(xk_msg_type type, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    msg_doit(type, fmt, ap);
    va_end(ap);
}

void msg_dbg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    msg_doit(XK_MSG_DEBUG, fmt, ap);
    va_end(ap);
}

void msg_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    msg_doit(XK_MSG_INFO, fmt, ap);
    va_end(ap);
}

void msg_notice(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    msg_doit(XK_MSG_NOTICE, fmt, ap);
    va_end(ap);
}


void msg_good(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    msg_doit(XK_MSG_GOOD, fmt, ap);
    va_end(ap);
}

void msg_bad(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    msg_doit(XK_MSG_BAD, fmt, ap);
    va_end(ap);
}

void msg_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    msg_doit(XK_MSG_ERROR, fmt, ap);
    va_end(ap);
}

