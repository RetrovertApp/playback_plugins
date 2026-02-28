#ifndef _UADE_COMPAT_AMIGA_H_
#define _UADE_COMPAT_AMIGA_H_

#ifdef __amigaos4__
#define __USE_INLINE__
#endif
#include <dos/dostags.h>
#include <proto/dos.h>

#if defined(__libnix__) || defined(__CLIB2__) || defined(__CLIB4__) || defined (__AROS__) || defined (__ixemul__)
// XXX __NEWLIB__ may get defined via sys includes even with other runtimes
#  ifdef __NEWLIB__
#    undef __NEWLIB__
#  endif
#endif

#if defined(__NEWLIB__)
// for ntohs
#include <netinet/in.h>
#endif

#if defined(__CLIB2__)
#warning "clib2 runtime not supported"
#endif

#if defined(__ixemul__)
#warning "ixemul runtime not supported"
#endif

#if defined(__AMIGA__) && defined(__NEWLIB__) && !defined(__amigaos4__)
#warning "newlib runtime not supported on AmigaOS3"
#endif

#endif
