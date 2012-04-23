/*

Copyright (c) '1999' RICE UNIVERSITY. All rights reserved 
Created by Vivek Sadananda Pai [vivek@cs.rice.edu], Departments of
Electrical and Computer Engineering and of Computer Science


This software, "Flash", is distributed to individuals for personal
non-commercial use and to non-profit entities for non-commercial
purposes only.  It is licensed on a non-exclusive basis, free of
charge for these uses.  All parties interested in any other use of the
software should contact the Rice University Office of Technology
Transfer [techtran@rice.edu]. The license is subject to the following
conditions:

1. No support will be provided by the developer or by Rice University.
2. Redistribution is not permitted. Rice will maintain a copy of Flash
   as a directly downloadable file, available under the terms of
   license specified in this agreement.
3. All advertising materials mentioning features or use of this
   software must display the following acknowledgment: "This product
   includes software developed by Rice University, Houston, Texas and
   its contributors."
4. Neither the name of the University nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY WILLIAM MARSH RICE UNIVERSITY, HOUSTON,
TEXAS, AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL RICE UNIVERSITY OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTIONS) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE), PRODUCT LIABILITY, OR OTHERWISE ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

This software contains components from computer code originally
created and copyrighted by Jef Poskanzer (C) 1995. The license under
which Rice obtained, used and modified that code is reproduced here in
accordance with the requirements of that license:

** Copyright (C) 1995 by Jef Poskanzer <jef@acme.com>.  All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "version.h"
#include "config.h"
#include "conn.h"
#include "loop.h"
#include "timer.h"
#include "hotname.h"
#include "handy.h"
#include "cgi.h"
#include "gscgi.h"
#include "readreq.h"
#include "libhttpd.h"
#include "nameconvert.h"

#ifdef CGI_TIMELIMIT
static void
CGITimeout2(httpd_conn* c)
{
  if (kill(c->hc_timerPrivate, SIGKILL) == 0)
    fprintf(stderr, "hard-killed CGI process %d\n", 
	    (int) c->hc_timerPrivate);
  else
    DoneWithConnection(c, TRUE);
}

static void
CGITimeout(httpd_conn* c)
{
  if (kill(c->hc_timerPrivate, SIGINT) == 0) {
    fprintf(stderr, "killed CGI process %d\n", (int) c->hc_timerPrivate);
    /* In case this isn't enough, schedule an uncatchable kill. */
    SetTimer(c, 5, CGITimeout2);
  }
  else
    DoneWithConnection(c, TRUE);
}
#endif /* CGI_TIMELIMIT */

#ifdef SERVER_NAME_LIST
static char*
hostname_map( char* hostname )
    {
    int len, n;
    static char* list[] = { SERVER_NAME_LIST };

    len = strlen( hostname );
    for ( n = sizeof(list) / sizeof(*list) - 1; n >= 0; --n )
	if ( strncasecmp( hostname, list[n], len ) == 0 )
	    if ( list[n][len] == '/' )	/* check in case of a substring match */
		return &list[n][len + 1];
    return (char*) 0;
    }
#endif /* SERVER_NAME_LIST */

/* ---------------------------------------------------------------- */
#define ENVP_SIZELOC 0		/* which element contains total size */
#define ENVP_NUMLOC 1		/* which element contains # of strings */
static int *
AddEnvPair(int *env, int *envAllocSize, char *name, char *value)
{
  /* 
     we assume the name has the = already

     basically, our environment "string" looks like the following
     [int] - size of string space in bytes + 2 size values
     [int] - # strings
     [string][0] - text of string followed by null
     [string][0] - text of string followed by null
     ...
     */

  int len;
  char *temp;
  int nameLen, valLen;

  if (*envAllocSize == 0) {
    *envAllocSize = 2000;
    env = malloc(*envAllocSize);
    if (env == NULL)
      return(NULL);
    env[ENVP_NUMLOC] = 0;
    env[ENVP_SIZELOC] = 2*sizeof(int);
  }

  if (!env)
    return(NULL);

  nameLen = strlen(name);
  valLen = strlen(value);
  len = nameLen + valLen + 1; /* for name, value, then (NUL) */

  env[ENVP_NUMLOC]++;
  while (env[ENVP_SIZELOC]+len > *envAllocSize) {
    int *newEnv;
    *envAllocSize *= 2;
    newEnv = realloc(env, *envAllocSize);
    if (newEnv == NULL) {
      free(env);
      return(NULL);
    }
    env = newEnv;
  }

  temp = ((char *) env) + env[ENVP_SIZELOC];
  memcpy(temp, name, nameLen);
  temp += nameLen;
  memcpy(temp, value, valLen);
  temp += valLen;
  *temp++ = '\0';
  env[ENVP_SIZELOC] += len;

  return(env);
}
/* ---------------------------------------------------------------- */
static int *
MakeSpecialEnvironment(httpd_conn* hc, char *stripped)
{
  /* since we're using a persistent cgi program, do _not_ ever
     conditionally include environment variables. Otherwise, the
     cgi process will have a very hard time deallocating previous
     environments */

  char* cp;
  char buf[256];
  int envAllocSize = 0;
  int *envPtr = NULL;		/* not strictly necessary */
  CGIInfo *cg;

  cg = hc->hc_cgiInfo;

  envPtr = AddEnvPair(envPtr, &envAllocSize, "PATH=", CGI_PATH);
  envPtr = AddEnvPair(envPtr, &envAllocSize, 
		      "SERVER_SOFTWARE=", SERVER_SOFTWARE);
  
  cp = HS.hostname;
#ifdef SERVER_NAME_LIST
  if (cp == (char*) 0 && gethostname(buf, sizeof(buf)) >= 0)
    cp = hostname_map(buf);
#endif /* SERVER_NAME_LIST */
  if (cp == (char*) 0) {
#ifdef SERVER_NAME
    cp = SERVER_NAME;
#else /* SERVER_NAME */
    if (gethostname(buf, sizeof(buf)) >= 0)
      cp = buf;
#endif /* SERVER_NAME */
  }
  
  envPtr = AddEnvPair(envPtr, &envAllocSize, "SERVER_NAME=", cp ? cp : "");
  envPtr = AddEnvPair(envPtr, &envAllocSize, "GATEWAY_INTERFACE=", "CGI/1.1");
  envPtr = AddEnvPair(envPtr, &envAllocSize, "SERVER_PROTOCOL=", 
		      hc->hc_protocol);
  sprintf(buf, "%d", HS.port);
  envPtr = AddEnvPair(envPtr, &envAllocSize, "SERVER_PORT=", buf);
  envPtr = AddEnvPair(envPtr, &envAllocSize, 
		      "REQUEST_METHOD=", HttpdMethodStr(hc->hc_method));
  if (cg->cgi_pathInfo[0] != '\0') {
    char* cp2;
    envPtr = AddEnvPair(envPtr, &envAllocSize, 
			"PATH_INFO=/", cg->cgi_pathInfo );
    cp2 = NEW(char, strlen(HS.cwd) + strlen(cg->cgi_pathInfo));
    if (cp2 != (char*) 0) {
      (void) sprintf(cp2, "%s%s", HS.cwd, cg->cgi_pathInfo);
      envPtr = AddEnvPair(envPtr, &envAllocSize, 
			  "PATH_TRANSLATED=", cp2);
      free(cp2);
    }
    else 
      envPtr = AddEnvPair(envPtr, &envAllocSize, "PATH_TRANSLATED=", "");
  }
  else {
    envPtr = AddEnvPair(envPtr, &envAllocSize, "PATH_INFO=", "");
    envPtr = AddEnvPair(envPtr, &envAllocSize, "PATH_TRANSLATED=", "");
  }
  
  envPtr = AddEnvPair(envPtr, &envAllocSize, 
		      "SCRIPT_NAME=/", stripped);
  envPtr = AddEnvPair(envPtr, &envAllocSize, "QUERY_STRING=", cg->cgi_query);
  envPtr = AddEnvPair(envPtr, &envAllocSize, 
		      "REMOTE_ADDR=", inet_ntoa(hc->hc_clientAddr));
  envPtr = AddEnvPair(envPtr, &envAllocSize, "HTTP_REFERER=", hc->hc_referer);
  envPtr = AddEnvPair(envPtr, &envAllocSize, 
		      "HTTP_USER_AGENT=", hc->hc_userAgent);
  envPtr = AddEnvPair(envPtr, &envAllocSize, "HTTP_ACCEPT=", hc->hc_accept);
  envPtr = AddEnvPair(envPtr, &envAllocSize, 
		      "HTTP_ACCEPT_ENCODING=", hc->hc_accepte);
  envPtr = AddEnvPair(envPtr, &envAllocSize, "HTTP_COOKIE=", hc->hc_cookie);
  envPtr = AddEnvPair(envPtr, &envAllocSize, 
		      "CONTENT_TYPE=", hc->hc_contentType);

  if (hc->hc_contentLength != -1) {
    sprintf(buf, "%d", hc->hc_contentLength);
    envPtr = AddEnvPair(envPtr, &envAllocSize, "CONTENT_LENGTH=", buf);
  }
  else 
    envPtr = AddEnvPair(envPtr, &envAllocSize, "CONTENT_LENGTH=", "");

  return(envPtr);
}
/* ---------------------------------------------------------------- */


/* Set up argument vector.  Again, we don't have to worry about freeing stuff
** since we're a sub-process.  This gets done after make_envp() because we
** scribble on hc->query.
*/
static char**
make_argp( httpd_conn* hc, char *expanded )
    {
    char** argp;
    int argn;
    char* cp1;
    char* cp2;

    /* By allocating an arg slot for every character in the query, plus
    ** one for the filename and one for the NULL, we are guaranteed to
    ** have enough.  We could actually use strlen/2.
    */
    argp = NEW( char*, strlen( hc->hc_cgiInfo->cgi_query ) + 2 );
    if ( argp == (char**) 0 )
	return (char**) 0;

    argp[0] = strrchr( expanded, '/' );
    if ( argp[0] != (char*) 0 )
	++argp[0];
    else
	argp[0] = expanded;

    argn = 1;
    /* According to the CGI spec at http://hoohoo.ncsa.uiuc.edu/cgi/cl.html,
    ** "The server should search the query information for a non-encoded =
    ** character to determine if the command line is to be used, if it finds
    ** one, the command line is not to be used."
    */
    if ( strchr( hc->hc_cgiInfo->cgi_query, '=' ) == (char*) 0 )
	{
	for ( cp1 = cp2 = hc->hc_cgiInfo->cgi_query; *cp2 != '\0'; ++cp2 )
	    {
	    if ( *cp2 == '+' )
		{
		*cp2 = '\0';
		StrDecode( cp1, cp1 );
		argp[argn++] = cp1;
		cp1 = cp2 + 1;
		}
	    }
	if ( cp2 != cp1 )
	    {
	    StrDecode( cp1, cp1 );
	    argp[argn++] = cp1;
	    }
	}

    argp[argn] = (char*) 0;
    return argp;
    }


/* ---------------------------------------------------------------- */
/*
   read all content data
   build environment data
   
   if no socket exists
   socketpair
   fork
   exec
   parent does:
   - select on socketpair
   - read socket file name
   - close connection
   child does:
   - create accept socket
   - send name
   - close connection


   once accept socket exists
    - select on connect
    - send all environment data
    - start read/write selects
      (send content data, read results)

   child does
   - wait on accept
   - reads environment
   - reads input, produces data
   - writes data
   - closes pipe

   once all results are back
   - close our end of socket
   - send all to client

*/
/* ---------------------------------------------------------------- */
static FreeSockNameInfo *freeSockHead;
/* ---------------------------------------------------------------- */
static FreeSockNameInfo *
GetFreeSockForCGI(char *name)
{
  /* given a cgi program's name, we return a pointer to the
     name of an accept socket for that program that's not in use */
  FreeSockNameInfo *temp;
  FreeSockNameInfo *walk;

  if (!freeSockHead)
    return(NULL);
  if (!strcmp(freeSockHead->fs_appName, name)) {
    temp = freeSockHead;
    freeSockHead = freeSockHead->fs_next;
    return(temp);
  }

  for (walk = freeSockHead; walk->fs_next; walk = walk->fs_next) {
    if (!strcmp(walk->fs_next->fs_appName, name)) {
      temp = walk->fs_next;
      walk->fs_next = temp->fs_next;
      return(temp);
    }
  }
  return(NULL);
}
/* ---------------------------------------------------------------- */
static void 
KillFreeSock(FreeSockNameInfo *fs)
{
  if (fs->fs_fd != -1) {
    close(fs->fs_fd);
    ClearConnFDAssociation(fs->fs_fd);
    SetSelectHandler(fs->fs_fd, NULL, SSH_NOTHING);
  }
  if (fs->fs_sockName) {
    unlink(fs->fs_sockName);
    free(fs->fs_sockName);
  }
  if (fs->fs_appName)
    free(fs->fs_appName);
  free(fs);
}
/* ---------------------------------------------------------------- */
void
KillAllFreeCGISocks(void)
{
  FreeSockNameInfo *dead;
  
  while (freeSockHead) {
    dead = freeSockHead;
    freeSockHead = dead->fs_next;
    KillFreeSock(dead);
  }
}
/* ---------------------------------------------------------------- */
static int 
IdleSockHandler(struct httpd_conn* c, int fd, int forRW)
{
  /* if this gets called, there was some activity on an "idle"
     free cgi socket. It's safe to assume the socket timed out
     and close it, because if it tried to write something, then
     it should be killed for an error 

     httpd_conn should be NULL - all we really have to go on is the fd */

  FreeSockNameInfo *dead;
  FreeSockNameInfo *fs;

  if (!freeSockHead) {
    fprintf(stderr, "IdleSockHandler: no freeSockHead\n");
    exit(-1);
  }

  if (freeSockHead->fs_fd == fd) {
    dead = freeSockHead;
    freeSockHead = dead->fs_next;
    KillFreeSock(dead);
    return(0);
  }

  for (fs = freeSockHead; fs->fs_next; fs = fs->fs_next) {
    if (fs->fs_next->fs_fd != fd)
      continue;
    dead = fs->fs_next;
    fs->fs_next = dead->fs_next;
    KillFreeSock(dead);
    return(0);
  }
  
  fprintf(stderr, "IdleSockHandler: couldn't match fd\n");
  exit(-1);
  return(-1);
}
/* ---------------------------------------------------------------- */
static void
AddFreeReadSockForCGI(FreeSockNameInfo **fsP)
{
  FreeSockNameInfo *temp;
  
  temp = *fsP;

  temp->fs_next = freeSockHead;
  freeSockHead = temp;

  ClearConnFDAssociation(temp->fs_fd);
  SetSelectHandler(temp->fs_fd, IdleSockHandler, SSH_READ);

  *fsP = NULL;
}
/* ---------------------------------------------------------------- */
static void
DisposeOfRetData(CGIInfo *cg)
{
  int i;

  for (i = 0; i < cg->cgi_numRetItems; i++) {
    if (!cg->cgi_retData[i].rds_data)
      continue;
    free(cg->cgi_retData[i].rds_data);
  }
}
/* ---------------------------------------------------------------- */
CGIInfo *
GetEmptyCGIInfo(void)
{
  return(calloc(1, sizeof(CGIInfo)));
}
/* ---------------------------------------------------------------- */
void
ReleaseCGIInfo(CGIInfo **cgP)
{
  CGIInfo *cg;

  cg = *cgP;

  if (cg->cgi_envInfo)
    free(cg->cgi_envInfo);

  if (cg->cgi_retData) {
    DisposeOfRetData(cg);
    free(cg->cgi_retData);
  }

  if (cg->cgi_workRetData)
    free(cg->cgi_workRetData);

  if (cg->cgi_contData)
    free(cg->cgi_contData);

  if (cg->cgi_fs) 
    KillFreeSock(cg->cgi_fs);

  if (cg->cgi_pathInfo)
    free(cg->cgi_pathInfo);
  if (cg->cgi_query)
    free(cg->cgi_query);

  free(cg);
  *cgP = NULL;
}
/* ---------------------------------------------------------------- */
#define HandleCGIFailure(p) HandleCGIFailurex(p, __LINE__)
static void
HandleCGIFailurex(struct httpd_conn *c, int d)
{
  fprintf(stderr, "cgi failure at line %d\n", d);
  DoneWithConnection(c, TRUE);
}
/* ---------------------------------------------------------------- */
static void
CleanupCGIOnSuccess(struct httpd_conn* c, CGIInfo *cg)
{
}
/* ---------------------------------------------------------------- */
static int 
WriteToClient(struct httpd_conn* c, int fd, int forRW)
{
  /* write to client, repeat until we've written it all
     if we've written it all, 
     - add socket name to free list
     - free our data structures
     - close or reset connection
     - set the select handler as appropriate
     */

  int res;
  CGIInfo *cg;
  int elementsToWrite;
  RetDataStruct *rds;

  cg = c->hc_cgiInfo;

  elementsToWrite = cg->cgi_numRetItems - cg->cgi_elementsWritten;

  if (elementsToWrite < 1) {
    /* we're done with the write to client */
    CleanupCGIOnSuccess(c, cg);
    DoneWithConnection(c, FALSE);
    return(0);
  }

  rds = &cg->cgi_workRetData[cg->cgi_elementsWritten];
  res = writev(c->hc_fd, rds, elementsToWrite);

  if (res < 1) {
    perror("res");
    HandleCGIFailure(c);
    return(-1);
  }

  while (res) {
    if (res < rds->rds_length) {
      rds->rds_length -= res;
      rds->rds_data += res;
      return(0);
    }
    cg->cgi_elementsWritten++;
    res -= rds->rds_length;
    rds++;
    if (cg->cgi_elementsWritten >= cg->cgi_numRetItems) {
      if (res) {
	fprintf(stderr, "still had res after writing cgi data back\n");
	exit(-1);
      }
      CleanupCGIOnSuccess(c, cg);
      DoneWithConnection(c, FALSE);
      return(0);
    }
  }

  /* code may actually reach here */
  return(0);
}
/* ---------------------------------------------------------------- */
static int
ReadCGIChunkHeader(httpd_conn *c, int fd, CGIInfo *cg, int *retVal,
		   int *keepReading)
{
  CGIChunkHeader ch;
  int res;
  
  res = read(fd, &ch, sizeof(ch));

  if (res < 0 && errno == EAGAIN) {
    *keepReading = FALSE;
    return(FALSE);
  }

  if (res < sizeof(ch)) {
    HandleCGIFailure(c);
    *retVal = -1;
    return(TRUE);
  }

  if (ch.ch_size < 1) {
    /* we're done reading */
    AddFreeReadSockForCGI(&cg->cgi_fs);

    cg->cgi_workRetData = 
      malloc(cg->cgi_numRetItems * sizeof(RetDataStruct));
    if (!cg->cgi_workRetData) {
      HandleCGIFailure(c);
      *retVal = -1;
      return(TRUE);
    }
    memcpy(cg->cgi_workRetData, cg->cgi_retData,
	   cg->cgi_numRetItems * sizeof(RetDataStruct));
    
    /* xxx we need to parse the result */
    cg->cgi_elementsWritten = 0;
    SetSelectHandler(c->hc_fd, WriteToClient, SSH_WRITE);
    *retVal = 0;
    return(TRUE);
  }
  cg->cgi_chunkBytesLeft = ch.ch_size;
  return(FALSE);
}
/* ---------------------------------------------------------------- */
static int
MakeRetSpaceIfNecessary(CGIInfo *cg)
{
  RetDataStruct *rds;
  int doInc;
  
  if (cg->cgi_numRetItems == 0)
    doInc = TRUE;
  else {
    rds = &cg->cgi_retData[cg->cgi_numRetItems-1];
    /* increment if we're starting or if we're full */
    doInc = (rds->rds_length == CGI_READSIZE);
  }
  
  if (!doInc)
    return(FALSE);

  /* if we don't have space, reallocate our array */
  if (cg->cgi_numRetItems == cg->cgi_numRetAlloc) {
    /* alloc new stuff */
    RetDataStruct *newRDS;
    cg->cgi_numRetAlloc *= 2;
    newRDS = realloc(cg->cgi_retData, 
		     cg->cgi_numRetAlloc * sizeof(RetDataStruct));
    if (!newRDS) 
      return(TRUE);
    cg->cgi_retData = newRDS;
  }
  cg->cgi_numRetItems++;
  rds = &cg->cgi_retData[cg->cgi_numRetItems-1];
  rds->rds_length = 0;
  return(FALSE);
}
/* ---------------------------------------------------------------- */
static int
ActuallyReadFromCGI(CGIInfo *cg, int fd, int *keepReading)
{
  void *readBufStart;
  int readBufLength;
  RetDataStruct *rds;
  int res;

  rds = &cg->cgi_retData[cg->cgi_numRetItems-1];

  /* allocate new read buffer if needed */
  if (!rds->rds_length) {
    rds->rds_data = malloc(CGI_READSIZE);
    if (!rds->rds_data) 
      return(TRUE);
  }
  
  /* figure out where to read and how much to read */
  readBufStart = (char *) rds->rds_data + rds->rds_length;
  readBufLength = CGI_READSIZE - rds->rds_length;
  if (readBufLength > cg->cgi_chunkBytesLeft)
    readBufLength = cg->cgi_chunkBytesLeft;
  
  res = read(fd, readBufStart, readBufLength);
  
  if (res > 0) {
    rds->rds_length += res;
    cg->cgi_chunkBytesLeft -= res;
  }
  
  /* if we just didn't have data available, that's OK */
  if (res < 0 && errno == EAGAIN) {
    *keepReading = FALSE;
    return(FALSE);
  }

  /* if it closed down or had some other problem, that's bad */
  if (res <= 0)
    return(TRUE);
  return(FALSE);
}
/* ---------------------------------------------------------------- */
static int 
ReadWriteHandler(struct httpd_conn* c, int fd, int forRW)
{
  /* try reading or writing data from/to cgi
     if we're finished writing, shut down one end of socket
     if we detect close, parse header and select on
     writing to client
     */
  CGIInfo *cg;
  int retVal = -1;

  cg = c->hc_cgiInfo;

  /* do the read before the write so we can figure out
     if the client closed */

  if (forRW & SSH_READ) {
    int keepReading = TRUE;
    do {
      if (!cg->cgi_chunkBytesLeft) {
	/* we need to read in a new chunk header */
	if (ReadCGIChunkHeader(c, fd, cg, &retVal, &keepReading))
	  return(retVal);
      }
      if (!keepReading)
	continue;
      if (MakeRetSpaceIfNecessary(cg)) {
	HandleCGIFailure(c);
	return(-1);
      }
      if (ActuallyReadFromCGI(cg, fd, &keepReading)) {
	HandleCGIFailure(c);
	return(-1);
      }
    } while (keepReading);
  }

  if (forRW & SSH_WRITE) {
    int res = 0;
    struct iovec *iov;
    int elementsToWrite;

    iov = &cg->cgi_contData[cg->cgi_elementsWritten];
    elementsToWrite = cg->cgi_numContDataElm - cg->cgi_elementsWritten;

    if (elementsToWrite) {
      res = writev(fd, iov, elementsToWrite);
      if (res <= 0) {
	HandleCGIFailure(c);
	return(-1);
      }
    }

    while (res) {
      if (res >= iov->iov_len) {
	res -= iov->iov_len;
	iov++;
	cg->cgi_elementsWritten++;
	if (cg->cgi_elementsWritten >= cg->cgi_numContDataElm)
	  break;
      }
      else {
	iov->iov_len -= res;
	iov->iov_base += res;
	res = 0;
      }
    }
    
    if (cg->cgi_elementsWritten >= cg->cgi_numContDataElm) {
      /* we're done sending data to the cgi program,
	 remove us from the writing select loop */
      if (res) {
	fprintf(stderr, "still had res after writing content data out\n");
	exit(-1);
      }
      SetSelectHandler(fd, ReadWriteHandler, SSH_READ);
      return(0);
    }

  }
  return(0);
}
/* ---------------------------------------------------------------- */
static int 
WriteEnvHandler(struct httpd_conn* c, int fd, int forRW)
{
  /*
     returns 0 if everything went OK, or -1 if there was some
     sort of problem */
  /* this means that we can write some part of environment data
     increment our bytecount
     if not finished, keep calling with ourselves set
     if finished,
     - set bytecount to 0
     - set resultcount to 0
     - select on read/writing
     */
  CGIInfo *cg;
  int totalSize;
  char *start;
  int res;

  cg = c->hc_cgiInfo;

  totalSize = cg->cgi_envInfo[ENVP_SIZELOC];
  start = ((char *)cg->cgi_envInfo) + cg->cgi_bytesWritten;

  res = write(cg->cgi_fs->fs_fd, start, totalSize - cg->cgi_bytesWritten);
  if (res < 0 && errno == EWOULDBLOCK) {
    /* assume it was an optimistic attempt */
    return(-1);
  }

  if (res <= 0) {
    HandleCGIFailure(c);
    return(-1);
  }

  cg->cgi_bytesWritten += res;
  if (cg->cgi_bytesWritten >= totalSize) {
    cg->cgi_elementsWritten = 0;
    if (cg->cgi_contData)
      SetSelectHandler(fd, ReadWriteHandler, SSH_WRITE | SSH_READ);
    else
      SetSelectHandler(fd, ReadWriteHandler, SSH_READ);
    return(0);
  }

  /* we set the handler since this function might have been
     called optimistically */
  SetSelectHandler(fd, WriteEnvHandler, SSH_WRITE);
  return(0);
}
/* ---------------------------------------------------------------- */
static int 
StartConnect(struct httpd_conn* c, CGIInfo *cg)
{
  static struct sockaddr_un saddrun;
  int sock;
  int res;

  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return(TRUE);
  }

  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl");
    close(sock);
    return(TRUE);
  }

  saddrun.sun_family = AF_UNIX;
  strcpy(saddrun.sun_path, cg->cgi_fs->fs_sockName);
  
  res = connect(sock, (struct sockaddr *) &saddrun, sizeof(saddrun));
  if (res < 0 && errno != EWOULDBLOCK) {
    close(sock);
    perror("connect");
    return(TRUE);
  }

  cg->cgi_fs->fs_fd = sock;
  MakeConnFDAssociation(c, sock);
  cg->cgi_bytesWritten = 0;

  if (WriteEnvHandler(c, sock, SSH_WRITE) < 0)
    SetSelectHandler(sock, WriteEnvHandler, SSH_WRITE);

  return(FALSE);
}
/* ---------------------------------------------------------------- */
static int 
ChildReadHandler(struct httpd_conn* c, int fd, int forRW)
{
  /* when this gets called, it means that the child has
     responded over our socketpair 
     
     read socket name
     close our end
     do a connect
     select on connect

     note: return value not used
     */
#define FREESOCKMAXNAME 200
  char freeSockName[FREESOCKMAXNAME];
  CGIInfo *cg;

  cg = c->hc_cgiInfo;
  
  freeSockName[FREESOCKMAXNAME-1] = '\0';
  if (read(fd, freeSockName, FREESOCKMAXNAME) < 1) {
    /* either close or error */
    HandleCGIFailure(c);
    return(-1);
  }

  if (freeSockName[FREESOCKMAXNAME-1] != '\0') {
    /* name was too long */
    HandleCGIFailure(c);
    return(-1);
  }

  cg->cgi_fs->fs_sockName = strdup(freeSockName);
  if (!cg->cgi_fs->fs_sockName) {
    HandleCGIFailure(c);
    return(-1);
  }

  close(fd);

  /* reset all fd info, since this end of socketpair is now closed */
  ClearConnFDAssociation(fd);
  SetSelectHandler(fd, NULL, SSH_NOTHING);
  cg->cgi_fs->fs_fd = -1;

  if (StartConnect(c, cg)) {
    HandleCGIFailure(c);
    return(-1);
  }
  return(0);
}
/* ---------------------------------------------------------------- */
static void
ConnectToExistingSock(struct httpd_conn* c, CGIInfo *cg)
{
  FreeSockNameInfo *fs;
  
  fs = cg->cgi_fs;
  MakeConnFDAssociation(c, fs->fs_fd);
  cg->cgi_bytesWritten = 0;

  if (WriteEnvHandler(c, fs->fs_fd, SSH_WRITE) < 0)
    SetSelectHandler(fs->fs_fd, WriteEnvHandler, SSH_WRITE);
}
/* ---------------------------------------------------------------- */
int
cgi(struct httpd_conn* hc, HotNameEntry **hotName, CGIInfo **cgP, 
    int addToHNE)
{
  int r;
  int sockVec[2];
  CGIInfo *cg;
  char *expanded;
  FreeSockNameInfo *fs;
  
  expanded = (*hotName)->hne_expanded;
  if (hc->hc_method == METHOD_HEAD) {
    /* HEAD doesn't make much sense for CGI.  We'll just make up a header
     ** and send it back.
     */
    SendMime(hc, 200, ok200title, "", "", "text/plain", -1, 0);
    return(0);
  }

  if (hc->hc_method != METHOD_GET && hc->hc_method != METHOD_POST) {
    HttpdSendErr(hc, 501, err501title, err501form, 
		 HttpdMethodStr(hc->hc_method));
    return -1;
  }

  cg = hc->hc_cgiInfo = *cgP;
  *cgP = NULL;

  if (hc->hc_contentLength < 1) 
    cg->cgi_contData = NULL;
  else {
    cg->cgi_contData = MakeContentDataIOV(hc, &cg->cgi_numContDataElm);
    if (!cg->cgi_contData) {
      HandleCGIFailure(hc);
      return(-1);
    }
  }

  cg->cgi_chunkBytesLeft = 0;
  cg->cgi_numRetItems = 0;	/* nothing read from cgi */
  cg->cgi_numRetAlloc = 5;
  cg->cgi_workRetData = NULL;
  cg->cgi_retData = malloc(cg->cgi_numRetAlloc * sizeof(RetDataStruct));
  cg->cgi_envInfo = MakeSpecialEnvironment(hc, 
					   (*hotName)->hne_stripped);
  if (!(cg->cgi_envInfo && cg->cgi_retData)) {
    fprintf(stderr, "error initially allocating cgi stuff\n");
    HandleCGIFailure(hc);
    return(-1);
  }

  /* we should add to the cache _before_ we block */
  if (addToHNE) {
    hc->hc_hne = *hotName;
    EnterIntoHotCGICache(hotName);
  }

  cg->cgi_fs = GetFreeSockForCGI(expanded);
  if (cg->cgi_fs) {
    /* if we have free socket,  do an connect on it */
    ConnectToExistingSock(hc, cg);
    return(0);
  }

  /* allocate a new structure */
  fs = cg->cgi_fs = calloc(1, sizeof(FreeSockNameInfo));
  if (!fs) {
    fprintf(stderr, "error allocating new cgi fs\n");
    HandleCGIFailure(hc);
    return(-1);
  }
  fs->fs_fd = -1;
  fs->fs_appName = strdup(expanded);
  if (!fs->fs_appName) {
    HandleCGIFailure(hc);
    return(-1);
  }

  /* launch a cgi program to create a new free accept socket */
  
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockVec) < 0) {
    fprintf(stderr, "error doing sockpair\n");
    return(-1);
  }
  
  r = fork();
  if (r < 0) {
    close(sockVec[0]);
    close(sockVec[1]);
    HandleCGIFailure(hc);
    fprintf(stderr, "error forking cgi\n");
    return(-1);
  }
  
  if (r == 0) {			/* we're in the child */
    char* binary;
    char* directory;
    char *args[] = {"", NULL};
    int i;

    close(sockVec[0]);		/* close parent's end */
    /*
       now, set the socket so it's the child's stdout
       close down everything else
       exec the child
       */

    /* we can call dup2 a little recklessly since we're
       about to exec... */
    if (dup2(sockVec[1], STDOUT_FILENO) < 0) {
      fprintf(stderr, "error while duping child\n");
      exit(-1);
    }
    if (dup2(sockVec[1], STDIN_FILENO) < 0) {
      fprintf(stderr, "error while duping child\n");
      exit(-1);
    }
    /*
    if (dup2(sockVec[1], STDERR_FILENO) < 0) {
      fprintf(stderr, "error while duping child\n");
      exit(-1);
    }
    */

    /* do this step after we've done all relevant dups -
       we'll close sockVec[1] in the process, but it's
       already been duped as needed */
    for (i = HttpdGetNFiles() - 1; i > 2; i--) 
      close(i);

    directory = strdup(expanded);
    if (directory == (char*) 0)
      binary = expanded;	/* ignore errors */
    else {
      binary = strrchr(directory, '/');
      if (binary == (char*) 0)
	binary = expanded;
      else {
	*binary++ = '\0';
	(void) chdir(directory); /* ignore errors */
      }
    }
    args[0] = binary;

    if (execv(binary, args) < 0) {
      fprintf(stderr, "error while execing child\n");
      exit(-1);
    }
    /* child never reaches here */
  }
  
  /* beyond this is the parent process. only */
  
  close(sockVec[1]);		/* close child's end */
  cg->cgi_fs->fs_fd = sockVec[0];	/* give parent zero, child one */

  /* now, we select on ChildReadHandler and wait until
     the child has created a socket for us */
  MakeConnFDAssociation(hc, cg->cgi_fs->fs_fd);
  SetSelectHandler(cg->cgi_fs->fs_fd, ChildReadHandler, SSH_READ);
 
#ifdef CGI_TIMELIMIT
  /* Schedule a kill for the child process, in case it runs too long */
  hc->hc_timerPrivate = r;
  SetTimer(hc, CGI_TIMELIMIT, CGITimeout);
#endif /* CGI_TIMELIMIT */
  return 0;

}
/* ---------------------------------------------------------------- */
