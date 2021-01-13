/*
 EvaFtplib.h - header file for callable EvaFtp access routines
 Copyright (C) 1996-2001, 2013, 2016 Thomas Pfau, tfpfau@gmail.com
	1407 Thomas Ave, North Brunswick, NJ, 08902
*/

#if !defined(__FTPLIB_H)
#define __FTPLIB_H

#if defined(__unix__) || defined(VMS)
#define GLOBALDEF
#define GLOBALREF extern
#elif defined(_WIN32)
#if defined BUILDING_LIBRARY
#define GLOBALDEF __declspec(dllexport)
#define GLOBALREF __declspec(dllexport)
#else
#define GLOBALREF __declspec(dllimport)
#endif
#endif

#include <limits.h>
#include <inttypes.h>

/* EvaFtpAccess() type codes */
#define EvaFtpLIB_DIR 1
#define EvaFtpLIB_DIR_VERBOSE 2
#define EvaFtpLIB_FILE_READ 3
#define EvaFtpLIB_FILE_WRITE 4

/* EvaFtpAccess() mode codes */
#define EvaFtpLIB_ASCII 'A'
#define EvaFtpLIB_IMAGE 'I'
#define EvaFtpLIB_TEXT EvaFtpLIB_ASCII
#define EvaFtpLIB_BINARY EvaFtpLIB_IMAGE

/* connection modes */
#define EvaFtpLIB_PASSIVE 1
#define EvaFtpLIB_PORT 2

/* connection option names */
#define EvaFtpLIB_CONNMODE 1
#define EvaFtpLIB_CALLBACK 2
#define EvaFtpLIB_IDLETIME 3
#define EvaFtpLIB_CALLBACKARG 4
#define EvaFtpLIB_CALLBACKBYTES 5

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__UINT64_MAX)
typedef uint64_t fsz_t;
#else
typedef uint32_t fsz_t;
#endif

typedef struct NetBuf netbuf;
typedef int (*EvaFtpCallback)(netbuf *nControl, fsz_t xfered, void *arg);

typedef struct EvaFtpCallbackOptions {
    EvaFtpCallback cbFunc;		/* function to call */
    void *cbArg;		/* argument to pass to function */
    unsigned int bytesXferred;	/* callback if this number of bytes transferred */
    unsigned int idleTime;	/* callback if this many milliseconds have elapsed */
} EvaFtpCallbackOptions;

GLOBALREF int EvaFtplib_debug;
GLOBALREF void EvaFtpVersion(void);
GLOBALREF char *FtpLastResponse(netbuf *nControl);
GLOBALREF int EvaFtpConnect(const char *host, netbuf **nControl);
GLOBALREF int EvaFtpOptions(int opt, long val, netbuf *nControl);
GLOBALREF int EvaFtpSetCallback(const EvaFtpCallbackOptions *opt, netbuf *nControl);
GLOBALREF int EvaFtpClearCallback(netbuf *nControl);
GLOBALREF int EvaFtpLogin(const char *user, const char *pass, netbuf *nControl);
GLOBALREF int EvaFtpAccess(const char *path, int typ, int mode, netbuf *nControl, netbuf **nData);
GLOBALREF int EvaFtpRead(char *buf, int max, netbuf *nData);
GLOBALREF int EvaFtpWrite(const char *buf, int len, netbuf *nData);
GLOBALREF int EvaFtpClose(netbuf *nData);
GLOBALREF int EvaFtpSysType(char *buf, int max, netbuf *nControl);
GLOBALREF int EvaFtpGet(const char *output, const char *path, char mode,	netbuf *nControl);
GLOBALREF int EvaFtpPut(const char *input, const char *path, char mode,	netbuf *nControl);
GLOBALREF void EvaFtpQuit(netbuf *nControl);

GLOBALDEF int EvaFtpREBOOT(netbuf *nControl);
GLOBALDEF int EvaFtpSETENV(const char *path, netbuf *nControl);
GLOBALDEF int EvaFtpGETENV(const char *path, netbuf *nControl);
GLOBALDEF int EvaFtpUNSETENV(const char *path, netbuf *nControl);
GLOBALDEF int EvaFtpBIN(netbuf *nControl);
GLOBALDEF int EvaFtpMediaFlash(netbuf *nControl);
GLOBALDEF int EvaFtpMediaSdram(netbuf *nControl);


GLOBALDEF int EvaFtpQuote(const char *path, netbuf *nControl);


#ifdef __cplusplus
};
#endif

#endif /* __FTPLIB_H */
