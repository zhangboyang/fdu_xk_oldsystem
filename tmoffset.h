#ifndef XK_TMOFFSET_H
#define XK_TMOFFSET_H

#include "xk.h"

int time_offset(time_t (*ask_time_func)(void *), void *ask_time_arg);

#endif
