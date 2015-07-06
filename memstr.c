#include <string.h>
#include "xk.h"
#include "memstr.h"

char *memstr(const char *haystack, const char *needle, int heystacklen)
{
    int i, j, needlelen = strlen(needle);
    /* heystacklen must be signed integer here */
    for (i = 0; i <= heystacklen - needlelen; i++) {
        for (j = 0; j < needlelen; j++)
            if (haystack[i + j] != needle[j])
                break;
        if (j >= needlelen)
            return (char *) haystack + i;
    }
    return NULL;
}
