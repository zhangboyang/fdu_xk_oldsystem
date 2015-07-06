#ifndef XK_TS_H
#define XK_TS_H

#include <sys/time.h>
#include "xk.h"

void ts_plus(struct timespec *tsp2, const struct timespec *tsp1, int ms);
int ts_minus(const struct timespec *tsp1, const struct timespec *tsp2);
int ts_less(const struct timespec *tsp1, const struct timespec *tsp2);

#endif
