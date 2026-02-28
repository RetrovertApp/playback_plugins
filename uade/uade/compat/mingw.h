#ifndef _UADE_COMPAT_MINGW_H_
#define _UADE_COMPAT_MINGW_H_

#include <winsock2.h>

#define realpath(N,R) _fullpath((R),(N),PATH_MAX)
#define mkdir(a,b) mkdir(a)
#define random rand
#define srandom srand

int dumb_socketpair(SOCKET socks[2], int make_overlapped);

#endif
