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


#include <sys/param.h>
#include <sys/types.h>
#include <netinet/in.h>

struct httpd_conn;
typedef void TimerFunc(struct httpd_conn *);

struct CacheEntry;
struct HeaderInfoStruct;

typedef struct httpd_conn {
  int hc_cnum;
  struct in_addr hc_clientAddr;
  int hc_method;
  int hc_status;
  
  /* these come from the very first header line */
  char *hc_encodedurl;
  char *hc_protocol;
  
  struct HotNameEntry *hc_hne;
  
  /* these are "simple" header lines - they optionally get
     filled from the contents of the request header, and there
     are not supposed to be multiple of them. */
  char *hc_cookie;
  char *hc_contentType;
  char *hc_referer;
  char *hc_userAgent;
  
  /* these are complicated header lines - the header may
     contain multiple entries which are supposed to be
     concatenated */
  char *hc_accept;
  char *hc_accepte;
  
  int hc_maxAccept, hc_maxAccepte;
  time_t hc_ifModifiedSince;
  int hc_contentLength;
  int hc_mimeFlag;
  int hc_fd;
  int hc_isPersistentConnection;
  struct HeaderInfoStruct *hc_headerInfo;
  struct CGIInfo *hc_cgiInfo;
  struct CacheEntry *hc_cacheEnt;
  struct DirStageBuf *hc_dsb;	/* staging buf for doing 'ls' */
  long hc_expirationTime;
  TimerFunc *hc_expireFunc;
  long hc_timerPrivate;		/* to be used by the TimerFunc if it wants */
  long hc_sndbuf;
  off_t hc_bytesSent;
  int hc_numChunkLocks;		/* have we set a chunk lock? */
  
  struct httpd_conn *hc_siblings; /* used in convert */
  struct httpd_conn *hc_next;	/* used in convert */
  char *hc_stripped;		/* used in convert */
  int hc_asyncByteRead;		/* used in async reading */
  int hc_asyncReadLen;		/* used in async reading */
  int hc_nagleOff;		/* has nagle been turned off already */

  char *hc_origFirstLine;	/* text of first line sent by client */
  int hc_origFirstLineLen;	/* length of first line text */
  int hc_maxOrigFirstLine;	/* size of buffer holding first line */
  int hc_neededDiskRead;	/* set on per-req basis if read needed */
  int hc_hadMincoreMisses;	/* set on per-req basis if mincore failed */
} httpd_conn;

/* keep these numbered from zero, in order of frequency */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_POST 2
#define METHOD_ERROR 3
#define NUM_METHODS 4

extern int numConnects;
extern int maxConnects;
extern int firstFreeConnHint;
extern char freeConnBits[];
extern httpd_conn* allConnects[];
#define ISCONNFREE(x) (freeConnBits[(x)>>3] & (1<<((x)&7)))
