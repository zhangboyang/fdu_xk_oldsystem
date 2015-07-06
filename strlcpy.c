#include <stdio.h>
#include <string.h>
#include "xk.h"
#include "strlcpy.h"

size_t strlcpy(char *dst, const char *src, size_t size)
{
    snprintf(dst, size, "%s", src);
    return strlen(src);
}

