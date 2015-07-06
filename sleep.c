#include <sys/time.h>
#include <sys/select.h>
#include <stdlib.h>
#include <time.h>
#include "xk.h"
#include "ts.h"
#include "sleep.h"

void sleep_ms(int ms)
{
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    select(0, NULL, NULL, NULL, &tv);
}

void busywait_until(struct timespec *tsp)
{
    struct timespec ts;
    while (1) {
        clock_gettime(CLOCK_REALTIME, &ts);
        if (!ts_less(&ts, tsp))
            break;
        if (ts_minus(tsp, &ts) > XK_SLEEP_BUSYWAITLIMIT)
            sleep_ms(XK_SLEEP_SLEEPTIME);
    }
}
