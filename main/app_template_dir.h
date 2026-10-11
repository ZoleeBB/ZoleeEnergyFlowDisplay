#pragma once
/* Small Win32 directory adapter; embedded/POSIX use the native implementation. */
#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
struct dirent { char d_name[MAX_PATH]; };
typedef struct { HANDLE handle; WIN32_FIND_DATAA data; struct dirent entry; int first; } DIR;
static inline DIR *opendir(const char *path){
    char pattern[MAX_PATH];
    if(snprintf(pattern,sizeof(pattern),"%s/*",path)>=(int)sizeof(pattern))return NULL;
    DIR *d=calloc(1,sizeof(*d));if(!d)return NULL;
    d->handle=FindFirstFileA(pattern,&d->data);d->first=1;
    if(d->handle==INVALID_HANDLE_VALUE){free(d);return NULL;}return d;
}
static inline struct dirent *readdir(DIR *d){
    if(!d->first&&!FindNextFileA(d->handle,&d->data))return NULL;
    d->first=0;snprintf(d->entry.d_name,sizeof(d->entry.d_name),"%s",d->data.cFileName);return &d->entry;
}
static inline void closedir(DIR *d){FindClose(d->handle);free(d);}
#ifndef S_ISREG
#define S_ISREG(m) (((m)&_S_IFMT)==_S_IFREG)
#endif
#else
#include <dirent.h>
#endif
