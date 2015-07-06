#include <time.h>
#include <sys/time.h>
#include "xk.h"
#include "ts.h"
#include "sleep.h"
#include "tmoffset.h"

static int test_offset(time_t (*ask_time_func)(void *), void *ask_time_arg,
                       int M, time_t *Tp, struct timespec *tsp)
{
    struct timespec dts;
    time_t curT;
    ts_plus(&dts, tsp, M);
    busywait_until(&dts);
    curT = ask_time_func(ask_time_arg);
    tsp->tv_sec++;
    (*Tp)++;
    return curT >= 0 ? curT == *Tp - 1 : -1;
}

int time_offset(time_t (*ask_time_func)(void *), void *ask_time_arg)
{
    /* calc remote time offset (in ms) using ask_time_func
     *  ask_time_func should return remote time
     *   or -1 if time is temporary unavaliable
     *  ask_time_func is called with argument ask_time_arg
     */
    
    struct timespec ts;
    int ret;
    int L, R, M;
    time_t T;
    
    clock_gettime(CLOCK_REALTIME, &ts);
    T = ask_time_func(ask_time_arg);
    L = 0;
    R = 1000;
    while (R - L > 1) {
        //printf("L=%d R=%d\n", L, R);
        M = (L + R) / 2;
        ret = test_offset(ask_time_func, ask_time_arg, M, &T, &ts);
        if (ret > 0)
            L = M;
        else if (ret == 0)
            R = M;
    }
    
    return (ts.tv_sec - T - 1) * 1000 + ts.tv_nsec / 1000000 + R;
}
