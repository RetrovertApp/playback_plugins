#include <stddef.h>
char *strcasestr(const char *s1, const char *s2)
{
    if (s1 == 0 || s2 == 0)
        return 0;

    size_t n = strlen(s2);
    while (*s1)
        if (!strncasecmp(s1++,s2,n))
            return (s1-1);

    return 0;
}
