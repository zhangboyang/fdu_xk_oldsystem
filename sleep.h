#ifndef XK_SLEEP_H
#define XK_SLEEP_H

#include "xk.h"

#define XK_SLEEP_BUSYWAITLIMIT 200 /* no sleep after this limit */
#define XK_SLEEP_SLEEPTIME 100 /* sleep time */

void sleep_ms(int ms);
void busywait_until(struct timespec *tsp);

#endif
