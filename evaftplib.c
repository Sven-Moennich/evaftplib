#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUILDING_LIBRARY
#include "evaftplib.h"
#include "version.h"

#if defined(__UINT64_MAX) && !defined(PRIu64)
#if ULONG_MAX == __UINT32_MAX
#define PRIu64 "llu"
#else
#define PRIu64 "lu"
#endif
#endif

#define SETSOCKOPT_OPTVAL_TYPE (void *)


#define FTPLIB_BUFSIZ 8192
#define RESPONSE_BUFSIZ 1024
#define TMP_BUFSIZ 1024
#define ACCEPT_TIMEOUT 30

#define EvaFtpLIB_CONTROL 0
#define EvaFtpLIB_READ 1
#define EvaFtpLIB_WRITE 2

#if !defined EvaFtpLIB_DEFMODE
#define EvaFtpLIB_DEFMODE EvaFtpLIB_PASSIVE
#endif

struct NetBuf {
    char *cput,*cget;
    int handle;
    int cavail,cleft;
    char *buf;
    int dir;
    netbuf *ctrl;
    netbuf *data;
    int cmode;
    struct timeval idletime;
    EvaFtpCallback idlecb;
    void *idlearg;
    unsigned long int xfered;
    unsigned long int cbbytes;
    unsigned long int xfered1;
    char response[RESPONSE_BUFSIZ];
};


GLOBALDEF int EvaFtplib_debug = 1;

int net_read(int fd, char *buf, size_t len) {
    while ( 1 ) {
	     int c = read(fd, buf, len);
	      if ( c == -1 ) {
	         if ( errno != EINTR && errno != EAGAIN )
		         return -1;
	          }	else	{
	             return c;
	          }
    }
}

int net_write(int fd, const char *buf, size_t len) {
    int done = 0;
    while ( len > 0 ) {
	int c = write( fd, buf, len );
	if ( c == -1 ) {
	    if ( errno != EINTR && errno != EAGAIN )
		return -1;
	} else if ( c == 0 ) {
	    return done;
	} else {
	    buf += c;
	    done += c;
	    len -= c;
	}
    }
    return done;
}

#define net_close close


/*
 * socket_wait - wait for socket to receive or flush data
 *
 * return 1 if no user callback, otherwise, return value returned by
 * user callback
 */
static int socket_wait(netbuf *ctl) {
    fd_set fd,*rfd = NULL,*wfd = NULL;
    struct timeval tv;
    int rv = 0;
    if ((ctl->dir == EvaFtpLIB_CONTROL) || (ctl->idlecb == NULL))
	return 1;
    if (ctl->dir == EvaFtpLIB_WRITE)
	wfd = &fd;
    else
	rfd = &fd;
    FD_ZERO(&fd);
    do {
	FD_SET(ctl->handle,&fd);
	tv = ctl->idletime;
	rv = select(ctl->handle+1, rfd, wfd, NULL, &tv);
	if (rv == -1) {
	    rv = 0;
	    strncpy(ctl->ctrl->response, strerror(errno),
                    sizeof(ctl->ctrl->response));
	    break;
	}	else if (rv > 0) {
	    rv = 1;
	    break;
	}
    }
    while ((rv = ctl->idlecb(ctl, ctl->xfered, ctl->idlearg)));
    return rv;
}

/*
 * read a line of text
 *
 * return -1 on error or bytecount
 */
static int readline(char *buf,int max,netbuf *ctl) {
    int x,retval = 0;
    char *end,*bp=buf;
    int eof = 0;

    if ((ctl->dir != EvaFtpLIB_CONTROL) && (ctl->dir != EvaFtpLIB_READ))
	return -1;
    if (max == 0)
	return 0;
    do {
    	if (ctl->cavail > 0) {
	    x = (max >= ctl->cavail) ? ctl->cavail : max-1;
	    end = (char*)memccpy(bp,ctl->cget,'\n',x);
	    if (end != NULL)
		x = end - bp;
	    retval += x;
	    bp += x;
	    *bp = '\0';
	    max -= x;
	    ctl->cget += x;
	    ctl->cavail -= x;
	    if (end != NULL) {
		bp -= 2;
		if (strcmp(bp,"\r\n") == 0) {
		    *bp++ = '\n';
		    *bp++ = '\0';
		    --retval;
		}
	    	break;
	    }
    	}
    	if (max == 1) {
	    *buf = '\0';
	    break;
    	}
    	if (ctl->cput == ctl->cget) {
	    ctl->cput = ctl->cget = ctl->buf;
	    ctl->cavail = 0;
	    ctl->cleft = FTPLIB_BUFSIZ;
    	}
	if (eof) {
	    if (retval == 0)
		retval = -1;
	    break;
	}
	if (!socket_wait(ctl))
	    return retval;
    	if ((x = net_read(ctl->handle,ctl->cput,ctl->cleft)) == -1) {
	    if (EvaFtplib_debug)
		perror("read");
	    retval = -1;
	    break;
    	}
	if (x == 0)
	    eof = 1;
    	ctl->cleft -= x;
    	ctl->cavail += x;
    	ctl->cput += x;
    }
    while (1);
    return retval;
}

/*
 * write lines of text
 *
 * return -1 on error or bytecount
 */
static int writeline(const char *buf, int len, netbuf *nData) {
    int x, nb=0, w;
    const char *ubp = buf;
    char *nbp;
    char lc=0;

    if (nData->dir != EvaFtpLIB_WRITE)
	return -1;
    nbp = nData->buf;
    for (x=0; x < len; x++) {
	if ((*ubp == '\n') && (lc != '\r'))	{
	    if (nb == FTPLIB_BUFSIZ) {
		if (!socket_wait(nData))
		    return x;
		w = net_write(nData->handle, nbp, FTPLIB_BUFSIZ);
		if (w != FTPLIB_BUFSIZ) {
		    if (EvaFtplib_debug)
			printf("net_write(1) returned %d, errno = %d\n", w, errno);
		    return(-1);
		}
		nb = 0;
	    }
	    nbp[nb++] = '\r';
	}
	if (nb == FTPLIB_BUFSIZ) {
	    if (!socket_wait(nData))
		return x;
	    w = net_write(nData->handle, nbp, FTPLIB_BUFSIZ);
	    if (w != FTPLIB_BUFSIZ) {
		if (EvaFtplib_debug)
		    printf("net_write(2) returned %d, errno = %d\n", w, errno);
		return(-1);
	    }
	    nb = 0;
	}
	nbp[nb++] = lc = *ubp++;
    }
    if (nb) {
	if (!socket_wait(nData))
	    return x;
	w = net_write(nData->handle, nbp, nb);
	if (w != nb) {
	    if (EvaFtplib_debug)
		printf("net_write(3) returned %d, errno = %d\n", w, errno);
	    return(-1);
	}
    }
    return len;
}

/*
 * read a response from the server
 *
 * return 0 if first char doesn't match
 * return 1 if first char matches
 */
static int readresp(char c, netbuf *nControl) {
    char match[5];
    if (readline(nControl->response,RESPONSE_BUFSIZ,nControl) == -1) {
	if (EvaFtplib_debug) perror("Control socket read failed");
	return 0;
    }
    if (EvaFtplib_debug > 1) fprintf(stderr,"%s",nControl->response);
    if (nControl->response[3] == '-') {
	     strncpy(match,nControl->response,3);
	     match[3] = ' ';
	     match[4] = '\0';
	     do {
	        if (readline(nControl->response,RESPONSE_BUFSIZ,nControl) == -1) {
		          if (EvaFtplib_debug) perror("Control socket read failed");
		          return 0;
	        }
	        if (EvaFtplib_debug > 1) fprintf(stderr,"%s",nControl->response);
	     }	while (strncmp(nControl->response,match,4));
    }
    if (nControl->response[0] == c)	return 1;
    return 0;
}

/*
 * EvaFtpVersion - print lib version
 */
GLOBALDEF void EvaFtpVersion(void) {
  printf("evaftplib-%s 11-Jan-2021, Sven Mönnich\n", VERSION );
}

/*
 * EvaFtpLastResponse - return a pointer to the last response received
 */
GLOBALDEF char *FtpLastResponse(netbuf *nControl) {
    if ((nControl) && (nControl->dir == EvaFtpLIB_CONTROL))
    	return nControl->response;
    return NULL;
}

/*
 * EvaFtpConnect - connect to remote server
 *
 * return 1 if connected, 0 if not
 */
GLOBALDEF int EvaFtpConnect(const char *host, netbuf **nControl) {
    int sControl;
    struct sockaddr_in sin;
    int on=1;
    netbuf *ctrl;
    char *lhost;
    char *pnum;

    memset(&sin,0,sizeof(sin));
    sin.sin_family = AF_INET;
    lhost = strdup(host);
    pnum = strchr(lhost,':');
    if (pnum == NULL)
	   pnum = "ftp";
    else
	*pnum++ = '\0';
    if (isdigit(*pnum))
	sin.sin_port = htons(atoi(pnum));
    else
    {
	struct servent *pse;
if ((pse = getservbyname(pnum,"tcp") ) == NULL )
	{
	    if ( EvaFtplib_debug )
		perror("getservbyname");
	    free(lhost);
	    return 0;
	}
	sin.sin_port = pse->s_port;
    }
    if ((sin.sin_addr.s_addr = inet_addr(lhost)) == INADDR_NONE)
    {
	struct hostent *phe;
    	if ((phe = gethostbyname(lhost)) == NULL)
    	{
	    if (EvaFtplib_debug)
		fprintf(stderr, "gethostbyname: %s\n", hstrerror(h_errno));
	    free(lhost);
	    return 0;
    	}
    	memcpy((char *)&sin.sin_addr, phe->h_addr, phe->h_length);
    }
    free(lhost);
    sControl = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sControl == -1)
    {
	if (EvaFtplib_debug)
	    perror("socket");
	return 0;
    }
    if (setsockopt(sControl,SOL_SOCKET,SO_REUSEADDR,
		   SETSOCKOPT_OPTVAL_TYPE &on, sizeof(on)) == -1)
    {
	if (EvaFtplib_debug)
	    perror("setsockopt");
	net_close(sControl);
	return 0;
    }
    if (connect(sControl, (struct sockaddr *)&sin, sizeof(sin)) == -1)
    {
	if (EvaFtplib_debug)
	    perror("connect");
	net_close(sControl);
	return 0;
    }
    ctrl = (netbuf*)calloc(1,sizeof(netbuf));
    if (ctrl == NULL)
    {
	if (EvaFtplib_debug)
	    perror("calloc");
	net_close(sControl);
	return 0;
    }
    ctrl->buf = (char*)malloc(FTPLIB_BUFSIZ);
    if (ctrl->buf == NULL)
    {
	if (EvaFtplib_debug)
	    perror("calloc");
	net_close(sControl);
	free(ctrl);
	return 0;
    }
    ctrl->handle = sControl;
    ctrl->dir = EvaFtpLIB_CONTROL;
    ctrl->ctrl = NULL;
    ctrl->data = NULL;
    ctrl->cmode = EvaFtpLIB_DEFMODE;
    ctrl->idlecb = NULL;
    ctrl->idletime.tv_sec = ctrl->idletime.tv_usec = 0;
    ctrl->idlearg = NULL;
    ctrl->xfered = 0;
    ctrl->xfered1 = 0;
    ctrl->cbbytes = 0;
    if (readresp('2', ctrl) == 0)
    {
	net_close(sControl);
	free(ctrl->buf);
	free(ctrl);
	return 0;
    }
    *nControl = ctrl;
    return 1;
}

GLOBALDEF int EvaFtpSetCallback(const EvaFtpCallbackOptions *opt, netbuf *nControl) {
    nControl->idlecb = opt->cbFunc;
    nControl->idlearg = opt->cbArg;
    nControl->idletime.tv_sec = opt->idleTime / 1000;
    nControl->idletime.tv_usec = (opt->idleTime % 1000) * 1000;
    nControl->cbbytes = opt->bytesXferred;
    return 1;
}

GLOBALDEF int EvaFtpClearCallback(netbuf *nControl) {
    nControl->idlecb = NULL;
    nControl->idlearg = NULL;
    nControl->idletime.tv_sec = 0;
    nControl->idletime.tv_usec = 0;
    nControl->cbbytes = 0;
    return 1;
}
/*
 * EvaFtpOptions - change connection options
 *
 * returns 1 if successful, 0 on error
 */
GLOBALDEF int EvaFtpOptions(int opt, long val, netbuf *nControl) {
    int v,rv=0;
    switch (opt) {
      case EvaFtpLIB_CONNMODE:
	     v = (int) val;
	     if ((v == EvaFtpLIB_PASSIVE) || (v == EvaFtpLIB_PORT)) {
	        nControl->cmode = v;
	        rv = 1;
	      }
	break;
      case EvaFtpLIB_CALLBACK:
	     nControl->idlecb = (EvaFtpCallback) val;
	     rv = 1;
	     break;
      case EvaFtpLIB_IDLETIME:
	v = (int) val;
	rv = 1;
	nControl->idletime.tv_sec = v / 1000;
	nControl->idletime.tv_usec = (v % 1000) * 1000;
	break;
      case EvaFtpLIB_CALLBACKARG:
	rv = 1;
	nControl->idlearg = (void *) val;
	break;
      case EvaFtpLIB_CALLBACKBYTES:
        rv = 1;
        nControl->cbbytes = (int) val;
        break;
    }
    return rv;
}

/*
 * EvaFtpSendCmd - send a command and wait for expected response
 *
 * return 1 if proper response received, 0 otherwise
 */
static int EvaFtpSendCmd(const char *cmd, char expresp, netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    if (nControl->dir != EvaFtpLIB_CONTROL)
	return 0;
    if (EvaFtplib_debug > 2)
	fprintf(stderr,"%s\n",cmd);
    if ((strlen(cmd) + 3) > sizeof(buf))
        return 0;
    sprintf(buf,"%s\r\n",cmd);
    if (net_write(nControl->handle,buf,strlen(buf)) <= 0) {
	     if (EvaFtplib_debug) perror("write");
	     return 0;
    }
    return readresp(expresp, nControl);
}

/*
 * EvaFtpLogin - log in to remote server
 *
 * return 1 if logged in, 0 otherwise
 */
GLOBALDEF int EvaFtpLogin(const char *user, const char *pass, netbuf *nControl) {
    char tempbuf[64];

    if (((strlen(user) + 7) > sizeof(tempbuf)) ||
        ((strlen(pass) + 7) > sizeof(tempbuf)))
        return 0;
    sprintf(tempbuf,"USER %s",user);
    if (!EvaFtpSendCmd(tempbuf,'3',nControl)) {
	if (nControl->response[0] == '2')
	    return 1;
	return 0;
    }
    sprintf(tempbuf,"PASS %s",pass);
    return EvaFtpSendCmd(tempbuf,'2',nControl);
}

/*
 * EvaFtpOpenPort - set up data connection
 *
 * return 1 if successful, 0 otherwise
 */
static int EvaFtpOpenPort(netbuf *nControl, netbuf **nData, int mode, int dir) {
    int sData;
    union {
	     struct sockaddr sa;
	     struct sockaddr_in in;
    } sin;
    struct linger lng = { 0, 0 };
    unsigned int l;
    int on=1;
    netbuf *ctrl;
    char *cp;
    unsigned int v[6];
    char buf[TMP_BUFSIZ];

    if (nControl->dir != EvaFtpLIB_CONTROL)	return -1;

    if ((dir != EvaFtpLIB_READ) && (dir != EvaFtpLIB_WRITE)) {
	     sprintf(nControl->response, "Invalid direction %d\n", dir);
	      return -1;
    }

    if ((mode != EvaFtpLIB_ASCII) && (mode != EvaFtpLIB_IMAGE)) {
	     sprintf(nControl->response, "Invalid mode %c\n", mode);
	      return -1;
    }

    l = sizeof(sin);
    if (nControl->cmode == EvaFtpLIB_PASSIVE) {
	     memset(&sin, 0, l);
	     sin.in.sin_family = AF_INET;
	     if (!EvaFtpSendCmd("PASV",'2',nControl)) return -1;
	     cp = strchr(nControl->response,'(');
	     if (cp == NULL) return -1;
	     cp++;
	     sscanf(cp,"%u,%u,%u,%u,%u,%u",&v[2],&v[3],&v[4],&v[5],&v[0],&v[1]);
	sin.sa.sa_data[2] = v[2];
	sin.sa.sa_data[3] = v[3];
	sin.sa.sa_data[4] = v[4];
	sin.sa.sa_data[5] = v[5];
	sin.sa.sa_data[0] = v[0];
	sin.sa.sa_data[1] = v[1];
    } else {
	if (getsockname(nControl->handle, &sin.sa, &l) < 0) {
	    if (EvaFtplib_debug)
		perror("getsockname");
	    return -1;
	}
    }
    sData = socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (sData == -1) {
	if (EvaFtplib_debug)
	    perror("socket");
	return -1;
    }
    if (setsockopt(sData,SOL_SOCKET,SO_REUSEADDR, SETSOCKOPT_OPTVAL_TYPE &on,sizeof(on)) == -1) {
	if (EvaFtplib_debug)
	    perror("setsockopt");
	net_close(sData);
	return -1;
    }
    if (setsockopt(sData,SOL_SOCKET,SO_LINGER, SETSOCKOPT_OPTVAL_TYPE &lng,sizeof(lng)) == -1) {
	if (EvaFtplib_debug)
	    perror("setsockopt");
	net_close(sData);
	return -1;
    }
    if (nControl->cmode == EvaFtpLIB_PASSIVE) {
	if (connect(sData, &sin.sa, sizeof(sin.sa)) == -1) {
	    if (EvaFtplib_debug)
		perror("connect");
	    net_close(sData);
	    return -1;
	}
    } else {
	sin.in.sin_port = 0;
	if (bind(sData, &sin.sa, sizeof(sin)) == -1) {
	    if (EvaFtplib_debug) perror("bind");
	    net_close(sData);
	    return -1;
	}
	if (listen(sData, 1) < 0) {
	    if (EvaFtplib_debug)
		perror("listen");
	    net_close(sData);
	    return -1;
	}
	if (getsockname(sData, &sin.sa, &l) < 0)
	    return -1;
	sprintf(buf, "PORT %d,%d,%d,%d,%d,%d",
		(unsigned char) sin.sa.sa_data[2],
		(unsigned char) sin.sa.sa_data[3],
		(unsigned char) sin.sa.sa_data[4],
		(unsigned char) sin.sa.sa_data[5],
		(unsigned char) sin.sa.sa_data[0],
		(unsigned char) sin.sa.sa_data[1]);
	if (!EvaFtpSendCmd(buf,'2',nControl))
	{
	    net_close(sData);
	    return -1;
	}
    }
    ctrl = (netbuf*)calloc(1,sizeof(netbuf));
    if (ctrl == NULL)
    {
	if (EvaFtplib_debug)
	    perror("calloc");
	net_close(sData);
	return -1;
    }
    if ((mode == 'A') && ((ctrl->buf = (char*)malloc(FTPLIB_BUFSIZ)) == NULL))
    {
	if (EvaFtplib_debug)
	    perror("calloc");
	net_close(sData);
	free(ctrl);
	return -1;
    }
    ctrl->handle = sData;
    ctrl->dir = dir;
    ctrl->idletime = nControl->idletime;
    ctrl->idlearg = nControl->idlearg;
    ctrl->xfered = 0;
    ctrl->xfered1 = 0;
    ctrl->cbbytes = nControl->cbbytes;
    ctrl->ctrl = nControl;
    if (ctrl->idletime.tv_sec || ctrl->idletime.tv_usec || ctrl->cbbytes)
	ctrl->idlecb = nControl->idlecb;
    else
	ctrl->idlecb = NULL;
    nControl->data = ctrl;
    *nData = ctrl;
    return 1;
}

/*
 * EvaFtpAcceptConnection - accept connection from server
 *
 * return 1 if successful, 0 otherwise
 */
static int EvaFtpAcceptConnection(netbuf *nData, netbuf *nControl) {
    int sData;
    struct sockaddr addr;
    unsigned int l;
    int i;
    struct timeval tv;
    fd_set mask;
    int rv;

    FD_ZERO(&mask);
    FD_SET(nControl->handle, &mask);
    FD_SET(nData->handle, &mask);
    tv.tv_usec = 0;
    tv.tv_sec = ACCEPT_TIMEOUT;
    i = nControl->handle;
    if (i < nData->handle)
	i = nData->handle;
    i = select(i+1, &mask, NULL, NULL, &tv);
    if (i == -1)
    {
        strncpy(nControl->response, strerror(errno),
                sizeof(nControl->response));
        net_close(nData->handle);
        nData->handle = 0;
        rv = 0;
    }
    else if (i == 0)
    {
	strcpy(nControl->response, "timed out waiting for connection");
	net_close(nData->handle);
	nData->handle = 0;
	rv = 0;
    }
    else
    {
	if (FD_ISSET(nData->handle, &mask))
	{
	    l = sizeof(addr);
	    sData = accept(nData->handle, &addr, &l);
	    i = errno;
	    net_close(nData->handle);
	    if (sData > 0)
	    {
		rv = 1;
		nData->handle = sData;
	    }
	    else
	    {
		strncpy(nControl->response, strerror(i),
                        sizeof(nControl->response));
		nData->handle = 0;
		rv = 0;
	    }
	}
	else if (FD_ISSET(nControl->handle, &mask))
	{
	    net_close(nData->handle);
	    nData->handle = 0;
	    readresp('2', nControl);
	    rv = 0;
	}
    }
    return rv;
}

/*
 * EvaFtpAccess - return a handle for a data stream
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpAccess(const char *path, int typ, int mode, netbuf *nControl, netbuf **nData) {
    char buf[TMP_BUFSIZ];
    int dir;
    if ((path == NULL) &&
        ((typ == EvaFtpLIB_FILE_WRITE) || (typ == EvaFtpLIB_FILE_READ)))
    {
	sprintf(nControl->response,
                "Missing path argument for file transfer\n");
	return 0;
    }
    sprintf(buf, "TYPE %c", mode);
    if (!EvaFtpSendCmd(buf, '2', nControl))
	return 0;
    switch (typ)
    {
      case EvaFtpLIB_DIR:
	strcpy(buf,"NLST");
	dir = EvaFtpLIB_READ;
	break;
      case EvaFtpLIB_DIR_VERBOSE:
	strcpy(buf,"LIST");
	dir = EvaFtpLIB_READ;
	break;
      case EvaFtpLIB_FILE_READ:
	strcpy(buf,"RETR");
	dir = EvaFtpLIB_READ;
	break;
      case EvaFtpLIB_FILE_WRITE:
	strcpy(buf,"STOR");
	dir = EvaFtpLIB_WRITE;
	break;
      default:
	sprintf(nControl->response, "Invalid open type %d\n", typ);
	return 0;
    }
    if (path != NULL)
    {
        int i = strlen(buf);
        buf[i++] = ' ';
        if ((strlen(path) + i + 1) >= sizeof(buf))
            return 0;
        strcpy(&buf[i],path);
    }
    if (EvaFtpOpenPort(nControl, nData, mode, dir) == -1)
	return 0;
    if (!EvaFtpSendCmd(buf, '1', nControl))
    {
	EvaFtpClose(*nData);
	*nData = NULL;
	return 0;
    }
    if (nControl->cmode == EvaFtpLIB_PORT)
    {
	if (!EvaFtpAcceptConnection(*nData,nControl))
	{
	    EvaFtpClose(*nData);
	    *nData = NULL;
	    nControl->data = NULL;
	    return 0;
	}
    }
    return 1;
}

/*
 * EvaFtpRead - read from a data connection
 */
GLOBALDEF int EvaFtpRead(char *buf, int max, netbuf *nData) {
    int i;
    if (nData->dir != EvaFtpLIB_READ)
	return 0;
    if (nData->buf) {
        i = readline(buf, max, nData);
    } else {
        i = socket_wait(nData);

	if (i != 1)
	    return 0;
        i = net_read(nData->handle, buf, max);
    }
    if (i == -1)
	return 0;
    nData->xfered += i;
    if (nData->idlecb && nData->cbbytes)
    {
        nData->xfered1 += i;
        if (nData->xfered1 > nData->cbbytes)
        {
	    if (nData->idlecb(nData, nData->xfered, nData->idlearg) == 0)
		return 0;
            nData->xfered1 = 0;
        }
    }
    return i;
}

/*
 * EvaFtpWrite - write to a data connection
 */
GLOBALDEF int EvaFtpWrite(const char *buf, int len, netbuf *nData) {
    int i;
    if (nData->dir != EvaFtpLIB_WRITE)
	return 0;
    if (nData->buf)
    	i = writeline(buf, len, nData);
    else
    {
        socket_wait(nData);
        i = net_write(nData->handle, buf, len);
    }
    if (i == -1)
	return 0;
    nData->xfered += i;
    if (nData->idlecb && nData->cbbytes)
    {
        nData->xfered1 += i;
        if (nData->xfered1 > nData->cbbytes)
        {
            nData->idlecb(nData, nData->xfered, nData->idlearg);
            nData->xfered1 = 0;
        }
    }
    return i;
}

/*
 * EvaFtpClose - close a data connection
 */
GLOBALDEF int EvaFtpClose(netbuf *nData) {
    netbuf *ctrl;
    switch (nData->dir)
    {
      case EvaFtpLIB_WRITE:
	/* potential problem - if buffer flush fails, how to notify user? */
	if (nData->buf != NULL)
	    writeline(NULL, 0, nData);
      case EvaFtpLIB_READ:
	if (nData->buf)
	    free(nData->buf);
	shutdown(nData->handle,2);
	net_close(nData->handle);
	ctrl = nData->ctrl;
	free(nData);
	ctrl->data = NULL;
	if (ctrl && ctrl->response[0] != '4' && ctrl->response[0] != '5')
	{
	    return(readresp('2', ctrl));
	}
	return 1;
      case EvaFtpLIB_CONTROL:
	if (nData->data)
	{
	    nData->ctrl = NULL;
	    EvaFtpClose(nData->data);
	}
	net_close(nData->handle);
	free(nData);
	return 0;
    }
    return 1;
}


/*
 * EvaFtpSysType - send a SYST command
 *
 * Fills in the user buffer with the remote system type.  If more
 * information from the response is required, the user can parse
 * it out of the response buffer returned by EvaFtpLastResponse().
 *
 * return 1 if command successful, 0 otherwise
 */
GLOBALDEF int EvaFtpSysType(char *buf, int max, netbuf *nControl){
    int l = max;
    char *b = buf;
    char *s;
    if (!EvaFtpSendCmd("SYST",'2',nControl)) return 0;
    s = &nControl->response[4];
    while ((--l) && (*s != ' ')) *b++ = *s++;
    *b++ = '\0';
    return 1;
}


/*
 * EvaFtpXfer - issue a command and transfer data
 *
 * return 1 if successful, 0 otherwise
 */
static int EvaFtpXfer(const char *localfile, const char *path, netbuf *nControl, int typ, int mode) {
    int l,c;
    char *dbuf;
    FILE *local = NULL;
    netbuf *nData;
    int rv=1;

    if (localfile != NULL) {
	     char ac[4];
       memset( ac, 0, sizeof(ac) );
       if (typ == EvaFtpLIB_FILE_WRITE) {
	        ac[0] = 'r';
        } else {
	        ac[0] = 'w';
        }

        if (mode == EvaFtpLIB_IMAGE) ac[1] = 'b';

        local = fopen(localfile, ac);
	      if (local == NULL)	{
	         strncpy(nControl->response, strerror(errno),
           sizeof(nControl->response));
	         return 0;
	      }
    }

          if (local == NULL) local = (typ == EvaFtpLIB_FILE_WRITE) ? stdin : stdout;

          if (!EvaFtpAccess(path, typ, mode, nControl, &nData)) {
               if (localfile)	{
                 fclose(local);
	               if ( typ == EvaFtpLIB_FILE_READ )
		               unlink(localfile);
	               }
	               return 0;
                }
                 dbuf = (char*)malloc(FTPLIB_BUFSIZ);
                 if (typ == EvaFtpLIB_FILE_WRITE) {
	                  while ((l = fread(dbuf, 1, FTPLIB_BUFSIZ, local)) > 0)	{
	                     if ((c = EvaFtpWrite(dbuf, l, nData)) < l) {
		                       printf("short write: passed %d, wrote %d\n", l, c);
		                       rv = 0;
		                       break;
	                     }
	                   }
                   } else {
    	                while ((l = EvaFtpRead(dbuf, FTPLIB_BUFSIZ, nData)) > 0) {
	                       if (fwrite(dbuf, 1, l, local) == 0) {
		                         if (EvaFtplib_debug)
		                           perror("localfile write");
		                         rv = 0;
		                         break;
	                        }
	                     }
                     }
    free(dbuf);
    fflush(local);
    if (localfile != NULL)
	fclose(local);
    EvaFtpClose(nData);
    return rv;
}

/*
 * EvaFtpGet - issue a GET command and write received data to output
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpGet(const char *outputfile, const char *path,	char mode, netbuf *nControl) {
    return EvaFtpXfer(outputfile, path, nControl, EvaFtpLIB_FILE_READ, mode);
}

/*
 * EvaFtpPut - issue a PUT command and send data from input
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpPut(const char *inputfile, const char *path, char mode,	netbuf *nControl) {
    return EvaFtpXfer(inputfile, path, nControl, EvaFtpLIB_FILE_WRITE, mode);
}


/*
 * EvaFtpQuit - disconnect from remote
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF void EvaFtpQuit(netbuf *nControl) {
    if (nControl->dir != EvaFtpLIB_CONTROL)	return;
    EvaFtpSendCmd("QUIT",'2',nControl);
    net_close(nControl->handle);
    free(nControl->buf);
    free(nControl);
}


/*
 * EvaFtpGETENV
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpGETENV(const char *path, netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    if ((strlen(path) + 7) > sizeof(buf)) return 0;
    sprintf(buf,"GETENV %s",path);
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}


/*
 * EvaFtpSETENV
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpSETENV(const char *path, netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    if ((strlen(path) + 6) > sizeof(buf)) return 0;
    sprintf(buf,"SETENV %s",path);
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}

/*
 * EvaFtpUNSETENV
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpUNSETENV(const char *path, netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    if ((strlen(path) + 6) > sizeof(buf)) return 0;
    sprintf(buf,"UNSETENV %s",path);
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}


/*
 * EvaFtpREBOOT - reboot the fritzbox
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpREBOOT(netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    sprintf(buf,"REBOOT");
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}

/*
 * EvaFtpSTATUS - reboot the fritzbox
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpBIN(netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    sprintf(buf,"BINARY");
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}

/*
 * EvaFtpMediaFLASH - reboot the fritzbox
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpMediaFlash(netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    sprintf(buf,"MEDIA FLSH");
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}

/*
 * EvaFtpMediaSdram - reboot the fritzbox
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpMediaSdram(netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    sprintf(buf,"MEDIA SDRAM");
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}


/*
 * EvaFtpQuote
 *
 * return 1 if successful, 0 otherwise
 */
GLOBALDEF int EvaFtpQuote(const char *path, netbuf *nControl) {
    char buf[TMP_BUFSIZ];
    if ((strlen(path) + 0) > sizeof(buf)) return 0;
    sprintf(buf,"%s",path);
    if (!EvaFtpSendCmd(buf,'2', nControl)) return 0;
    return 1;
}
