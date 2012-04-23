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



#include "config.h"
#include "version.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#include "libhttpd.h"
#include "match.h"
#include "tdate_parse.h"
#include "hotname.h"
#include "datacache.h"
#include "handy.h"
#include "cgi.h"
#include "conn.h"

char *methodStrings[NUM_METHODS];

void TimeEnd(char *);
FILE *timingFile;
#define TIMINGFILENAME "/tmp/timingfile"

struct timeval globalTimeOfDay;
char globalTimeOfDayStr[40];	/* GMT time in HTTP format */

static struct in_addr host_addr;

int defaultSendBufSize;

static int scrambledNumbers[256];

/* ---------------------------------------------------------------- */
int
GenericStringHash(unsigned char *string)
{
  int val;
  int i, j;

  j = 0;
  val = 0;

  while (*string) {
    i = (j + *string) & 0xff;
    j += 163;			/* some prime number */
    val += scrambledNumbers[i];
    string++;
  }
  return(val);
}
/* ---------------------------------------------------------------- */
void
MakeHTTPDate(char *buf, time_t unixTime)
{
  /* date generated by this function will be constant-length.
     if it's not, very bad things can happen */

  static char *monthNames[] = {
    "Jan", "Feb", "Mar", "Apr", 
    "May", "Jun", "Jul", "Aug", 
    "Sep", "Oct", "Nov", "Dec"};
  static char *dayNames[] = {
    "Sun", "Mon", "Tue", 
    "Wed", "Thu", "Fri", "Sat"};
  struct tm *gmt;
  
  gmt = gmtime(&unixTime);

  sprintf(buf, "%s, %.2d %s %d %.2d:%.2d:%.2d GMT", 
	  dayNames[gmt->tm_wday], 
	  gmt->tm_mday, monthNames[gmt->tm_mon], gmt->tm_year + 1900,
	  gmt->tm_hour, gmt->tm_min, gmt->tm_sec);
}
/* ---------------------------------------------------------------- */
static void
ChildHandler(int sig)
{
  pid_t pid;
  int status;
  
  /* Reap defunct children until there aren't any more. */
  for (;;) {
#ifdef HAVE_WAITPID
    pid = waitpid(-1, &status, WNOHANG);
#else /* HAVE_WAITPID */
    pid = wait3(&status, WNOHANG, NULL);
#endif /* HAVE_WAITPID */
    if (!pid)			/* none left */
      break;
    if (pid < 0) {
      if (errno == EINTR)	/* because of ptrace */
	continue;
      /* ECHILD shouldn't happen with the WNOHANG option, but with
      ** some kernels it does anyway.  Ignore it.
      */
      if (errno != ECHILD)
	perror("waitpid in childhandler");
      break;
    }
  }

  /* reset the signal _after_ getting the kids - some systems
     seem to dislike it the other way around */
  signal(SIGCHLD, ChildHandler);  
}
/* ---------------------------------------------------------------- */
static void
FreeHttpdServer(void)
{
  if (HS.cwd)
    free(HS.cwd);
  if (HS.cgi_pattern)
    free(HS.cgi_pattern);
}
/* ---------------------------------------------------------------- */
int
HttpdInitialize(char* hostname, int port, char* cgi_pattern, char* cwd)
{
  int on;
  struct hostent* he;
  struct sockaddr_in sa;
  char* cp;
  struct linger Ling;
  size_t sz;
  int so_sndbuf;
  int i;

  for (i = 0; i < 256; i++)
    scrambledNumbers[i] = random();

  methodStrings[METHOD_GET] = "GET";
  methodStrings[METHOD_POST] = "POST";
  methodStrings[METHOD_HEAD] = "HEAD";
  methodStrings[METHOD_ERROR] = "";
  
  /* Set up child-process reaper. */
  signal(SIGCHLD, ChildHandler);
  
  if (!hostname)
    HS.hostname = NULL;
  else
    HS.hostname = strdup(hostname);
  HS.port = port;
  if (!cgi_pattern)
    HS.cgi_pattern = NULL;
  else {
    /* Nuke any leading slashes. */
    if (cgi_pattern[0] == '/')
      ++cgi_pattern;
    HS.cgi_pattern = strdup(cgi_pattern);
    if (HS.cgi_pattern == (char*) 0) {
      fprintf(stderr, "out of memory\n");
      return(TRUE);
    }
    /* Nuke any leading slashes in the cgi pattern. */
    while ((cp = strstr(HS.cgi_pattern, "|/")) != NULL)
      strcpy(cp + 1, cp + 2);
  }
  HS.cwd = strdup(cwd);
  if (HS.cwd == (char*) 0) {
    fprintf(stderr, "out of memory\n");
    return(TRUE);
  }
  
  /* Create socket. */
  HS.fd = socket(AF_INET, SOCK_STREAM, 0);
  if (HS.fd < 0) {
    perror("creating accept socket");
    FreeHttpdServer();
    return(TRUE);
  }
  
  /* Allow reuse of local addresses. */
  on = 1;
  if (setsockopt(HS.fd, SOL_SOCKET, SO_REUSEADDR, (char*) &on, sizeof(on)) < 0)
    perror("setsockopt SO_REUSEADDR");
  
  Ling.l_onoff = 0;              /* off */
  Ling.l_linger = 0;
  
  if (setsockopt(HS.fd, SOL_SOCKET, SO_LINGER, (char *) &Ling, sizeof(Ling)))
    perror("linger");
  
  sz = sizeof(so_sndbuf);
  if (getsockopt(HS.fd, SOL_SOCKET, SO_SNDBUF,
		  (char*) &so_sndbuf, &sz) < 0) {
    perror("getsockopt SO_SNDBUF");
    exit(-1);
  }

  defaultSendBufSize = so_sndbuf;
  if (sendBufSizeBytes <= defaultSendBufSize)
    sendBufSizeBytes = 0;
  
  /* Set the accept socket file descriptor to no-delay mode. */
  if (fcntl(HS.fd, F_SETFL, O_NDELAY) < 0) {
    perror("can't get accept into no-delay");
    exit(-1);
  }
  
  /* Bind to it. */
  memset((char*) &sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  if (HS.hostname == (char*) 0)
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
  else {
    sa.sin_addr.s_addr = inet_addr(HS.hostname);
    if ((int) sa.sin_addr.s_addr == -1) {
      he = gethostbyname(HS.hostname);
      if (he == (struct hostent*) 0) {
	perror("gethostbyname");
	FreeHttpdServer();
	return(TRUE);
      }
      if (he->h_addrtype != AF_INET ||
	   he->h_length != sizeof(sa.sin_addr)) {
	fprintf(stderr, "%.80s - non-IP network address\n", HS.hostname);
	FreeHttpdServer();
	return(TRUE);
      }
      memcpy(&sa.sin_addr.s_addr, he->h_addr, sizeof(sa.sin_addr));
    }
  }
  sa.sin_port = htons(HS.port);
  host_addr = sa.sin_addr;
  if (bind(HS.fd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    perror("binding accept socket");
    close(HS.fd);
    FreeHttpdServer();
    return(TRUE);
  }
  
  /* Start a listen going. */
  if (listen(HS.fd, LISTEN_BACKLOG) < 0) {
    perror("listen on accept socket");
    close(HS.fd);
    FreeHttpdServer();
    return(TRUE);
  }
  
  return(FALSE);
}
/* ---------------------------------------------------------------- */
void
HttpdTerminate(void)
{
  close(HS.fd);
  FreeHttpdServer();
}
/* ---------------------------------------------------------------- */
char* ok200title = "OK";

char* err302title = "Found";
char* err302form = "The actual URL is '%.80s'.\n";

char* err304title = "Not Modified";

char* err400title = "Bad Request";
char* err400form =  "Your request '%.80s' has bad syntax or is inherently impossible to satisfy.\n";

char* err403title = "Forbidden";
char* err403form =  "You do not have permission to get URL '%.80s' from this server.\n";

char* err404title = "Not Found";
char* err404form =  "The requested URL '%.80s' was not found on this server.\n";

char* err408title = "Request Timeout";
char* err408form =  "No request appeared within a reasonable time period.\n";

char* err500title = "Internal Error";
char* err500form =  "There was an unusual problem serving the requested URL '%.80s'.\n";

char* err501title = "Not Implemented";
char* err501form =  "The requested method '%.80s' is not implemented by this server.\n";

char* httpd_err503title = "Service Temporarily Overloaded";
char* httpd_err503form =  "The requested URL '%.80s' is temporarily overloaded.  Please try again later.\n";
/* ---------------------------------------------------------------- */
void
SendMime(httpd_conn* hc, int status, char* title, char* encodings, 
	  char* extraheads, char* type, int length, time_t mod)
{
  char *enc_head = "\nContent-Encoding: ";
  char tbuf[100];
  char buf[1000];
  char conLenInfo[40];
  
  if (encodings[0] == '\0')
    encodings = enc_head = "";

  if (length >= 0) 
    sprintf(conLenInfo, "\nContent-Length: %d", length);
  else 
    conLenInfo[0] = '\0';

  hc->hc_status = status;
  if (hc->hc_mimeFlag) {
    if (mod == (time_t) 0)
      mod = time((time_t*) 0);
    MakeHTTPDate(tbuf, mod);

    sprintf(buf, 
	    "HTTP/1.0 %d %s\n"
	    "Date: %s\n"
	    "Server: %s%s%s%s%s\n"
	    "Content-Type: %s%s\n"
	    "Last-Modified: %s\n\n",
	    status, title, 
	    globalTimeOfDayStr,
	    SERVER_SOFTWARE,
	    enc_head, encodings, 
	    extraheads[0] != '\0' ? "\n" : "", extraheads,
	    type, conLenInfo, tbuf);
    write(hc->hc_fd, buf, strlen(buf));
  }
}
/* ---------------------------------------------------------------- */
static int
SendAddress(httpd_conn* hc)
{
  char buf[1000];
  int len;
  
  sprintf(buf,
	   "<ADDRESS><A HREF=\"%s\">%s</A></ADDRESS>\n",
	   SERVER_ADDRESS, SERVER_SOFTWARE);
  len = strlen(buf);
  write(hc->hc_fd, buf, len);
  return len;
}
/* ---------------------------------------------------------------- */
char *
realloc_str(char** strP, int* maxsizeP, int size)
{
  char *temp;

  if (*maxsizeP == 0) {
    *maxsizeP = MAX(30, size);	/* arbitrary */
    temp = NEW(char, *maxsizeP + 1);
    if (temp)
      temp[0] = '\0';
  }
  else if (size > *maxsizeP) {
    *maxsizeP = MAX(*maxsizeP * 2, size * 5 / 4);
    temp = RENEW(*strP, char, *maxsizeP + 1);
  }
  else
    return(*strP);
  if (temp == (char*) 0)
    fprintf(stderr, "out of memory in realloc_str\n");
  else
    *strP = temp;
  
  return(temp);
}
/* ---------------------------------------------------------------- */
static void
SendResponse(httpd_conn* hc, int status, char* title, 
	     char* extrahead, char* form, char* arg)
{
  char buf[1000];
  
  SendMime(hc, status, title, "", extrahead, "text/html", -1, 0);
  sprintf(buf, 
	  "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\n"
	  "<BODY><H2>%d %s</H2>\n",
	  status, title, status, title);
  write(hc->hc_fd, buf, strlen(buf));
  sprintf(buf, form, arg);
  write(hc->hc_fd, buf, strlen(buf));
  sprintf(buf, "<HR>\n");
  write(hc->hc_fd, buf, strlen(buf));
  SendAddress(hc);
  sprintf(buf, "</BODY></HTML>\n");
  write(hc->hc_fd, buf, strlen(buf));
}
/* ---------------------------------------------------------------- */
void
HttpdSendErr(httpd_conn* hc, int status, char* title, char* form, char* arg)
{
  SendResponse(hc, status, title, "", form, arg);
}
/* ---------------------------------------------------------------- */
void
SendDirRedirect(httpd_conn* hc)
{
  static char* location;
  static char* header;
  static int maxlocation = 0, maxheader = 0;
  static char headstr[] = "Location: ";
  
  if (!realloc_str(&location, &maxlocation, strlen(hc->hc_encodedurl)+1)) {
    HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    return;
  }
  sprintf(location, "%s/", hc->hc_encodedurl);

  if (realloc_str(&header, &maxheader, sizeof(headstr) + strlen(location))) {
    HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    return;
  }
  sprintf(header, "%s%s", headstr, location);
  
  SendResponse(hc, 302, err302title, header, err302form, location);
}
/* ---------------------------------------------------------------- */
char*
HttpdMethodStr(int method)
{
  switch (method) {
  case METHOD_GET: return "GET";
  case METHOD_HEAD: return "HEAD";
  case METHOD_POST: return "POST";
  default: return (char*) 0;
  }
}
/* ---------------------------------------------------------------- */
int
HttpdGetNFiles(void)
{
  static int inited = 0;
  static int n;
  
  if (! inited) {
#ifdef RLIMIT_NOFILE
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
      perror("getrlimit");
      exit(1);
    }
    if (rl.rlim_max == RLIM_INFINITY)
      rl.rlim_cur = 4096;		/* arbitrary */
    else
      rl.rlim_cur = rl.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
      perror("setrlimit");
      exit(1);
    }
    n = rl.rlim_cur;
#else /* RLIMIT_NOFILE */
    n = getdtablesize();
#endif /* RLIMIT_NOFILE */
    inited = 1;
  }
  return n;
}
/* ---------------------------------------------------------------- */
float
DiffTime(struct timeval *start, struct timeval *end)
{
  float temp;

  temp = (end->tv_sec - start->tv_sec) + 
    1e-6 * (end->tv_usec - start->tv_usec);
  return(temp);
}
/* ---------------------------------------------------------------- */

