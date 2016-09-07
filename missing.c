#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <windows.h>
#include <io.h>
/*
 * Yuck; enough for our purposes, however
 */
size_t __mingw_vfprintf(FILE *fp, char *x, va_list arglist)
{
char fbuf[BUFSIZ];

    (void) _vsnprintf(fbuf, BUFSIZ, x, arglist);
    return fwrite(fbuf, sizeof(char), strlen(fbuf), fp);
}
