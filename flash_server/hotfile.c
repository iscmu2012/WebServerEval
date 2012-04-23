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


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include "datacache.h"
#include <sys/mman.h>
#include "conn.h"
#include "handy.h"
#include "loop.h"
#include "libhttpd.h"
#include "hotfile.h"
#include "helper.h"
#include "config.h"
#include "timer.h"
#include "hotname.h"
#include "cold.h"
#include "common.h"

/* ---------------------------------------------------------------- */
void
ResumeAfterFileReading(httpd_conn *c)
{
  /* called after slave has read appropriate chunk */
  if (ReadChunkIfNeeded(c->hc_cacheEnt, c->hc_bytesSent)) {
    fprintf(stderr, "failed in ResumeAfterFileReading\n");
    DoneWithConnection(c, TRUE);
    return;
  }
  SetSelectHandler(c->hc_fd, DoSingleWrite, SSH_WRITE);
}
/* ---------------------------------------------------------------- */
static long
PowerOf2LessThanEqual(long x)
{
  long y = 1;

  /* test if it's power of 2 already*/
  if ((x & (x-1)) == 0)
    return(x);

  while (y <= x)
    y <<=1;
  return(y>>1);
}
/* ---------------------------------------------------------------- */
int 
DoSingleWrite(httpd_conn* c, int fd, int forRW)
{
  DoSingleWriteBackend(c, fd, FALSE);
  return(0);
}
/* ---------------------------------------------------------------- */
void 
DoSingleWriteBackend(httpd_conn* c, int fd, int testing)
{
  void *dataToSend; 
  int bytesToSend;
  int startOffset;
  int maxBufSize, targetSize;
  int sz;
  struct iovec ioBufs[2];
  int numIOBufs = 0;
  char *incoreStart;
  int incoreLen;
  int i;
#define DATESTARTOFF 22		/* offset of actual date field */
#define DATESTRLEN 27		/* length of date information */

  /* if the send buffer is too small, size it appropriately */
  if (sendBufSizeBytes &&
      c->hc_sndbuf < c->hc_cacheEnt->ce_size &&
      c->hc_sndbuf != sendBufSizeBytes) {
    int so_sndbuf;
    int sz = sizeof(so_sndbuf);
    
    c->hc_sndbuf = sendBufSizeBytes;

    so_sndbuf = sendBufSizeBytes;
    if (setsockopt(c->hc_fd, SOL_SOCKET, SO_SNDBUF,
		   (char*) &so_sndbuf, sz) < 0) 
      fprintf(stderr, "setsockopt SO_SNDBUF\n");
  }

  /* Write a bufferload. */
  if (c->hc_bytesSent == 0 && c->hc_mimeFlag &&
      c->hc_cacheEnt->ce_respHeaderLen) 
    maxBufSize = c->hc_sndbuf - c->hc_cacheEnt->ce_respHeaderLen;
  else 
    maxBufSize = c->hc_sndbuf;
  
  targetSize = PowerOf2LessThanEqual(maxBufSize);

  dataToSend = GetDataToSend(c->hc_cacheEnt, c->hc_bytesSent, targetSize, 
			     maxBufSize, &bytesToSend, &startOffset);

  if (bytesToSend && (dataToSend == NULL)) {
    /* data not in memory - start reading */
    ScheduleAsyncRead(c, c->hc_bytesSent, bytesToSend);
    c->hc_neededDiskRead = 1;
    return;
  }

#if defined(__osf__) || defined(__linux__)
#define SKIP_MINCORE
#endif

#ifndef SKIP_MINCORE		/* define this to avoid compiling mincore */
  if (bytesToSend) {
    static char *incoreRes;
    /* round start down to page boundary, round length up */
    incoreStart = (char *) dataToSend  + startOffset;
    incoreLen = ((long) incoreStart) & (systemPageSize-1);
    incoreStart -= incoreLen;
    incoreLen += bytesToSend + systemPageSize-1;
    incoreLen -= incoreLen & (systemPageSize-1);
    
    if (!incoreRes) {
      incoreRes = calloc(1, 10 + READBLOCKSIZE/systemPageSize); /* slop */
      if (!incoreRes)
	Panic("failed allocating incore");
    }

    if (mincore(incoreStart, incoreLen, incoreRes)) {
      fprintf(stderr, "mincore had error\n");
      exit(-1);
    }
    
    incoreLen >>= systemPageSizeBits;
    for (i = 0; i < incoreLen; i++) {
      if (!(incoreRes[i]&1)) {
	if (doMincoreDump)
	  putc((i < 10) ? ('0'+i) : ('a'+i-10), stderr);
	if (!i) {
	  /* no data in memory - start reading */
	  ScheduleAsyncRead(c, c->hc_bytesSent, bytesToSend);
	  c->hc_neededDiskRead = 1;
	  c->hc_hadMincoreMisses = 1;
	  return;
	}
	else {
	  /* use what we have first */
	  char *incoreEnd;
	  incoreEnd = incoreStart + (i << systemPageSizeBits);
	  bytesToSend = incoreEnd - ((char *) dataToSend + startOffset);
	  break;
	}
      }
    }
  }
#endif

  if (bytesToSend == 0 && c->hc_cacheEnt->ce_size) {
    /* if the file has a size _and_ we don't have anything
       to send, then we're done - otherwise, it might be
       a zero-byte file */
    DoneWithConnection(c, TRUE);
    return;
  }

  if (c->hc_bytesSent==0 && c->hc_cacheEnt->ce_respHeaderLen) {
    ioBufs[0].iov_base = c->hc_cacheEnt->ce_respHeader;
    ioBufs[0].iov_len = c->hc_cacheEnt->ce_respHeaderLen;
    numIOBufs = 1;

    if (c->hc_cacheEnt->ce_respHeaderTime != globalTimeOfDay.tv_sec) { 
      /* fill in the current time in header */
      memcpy( ((char *)ioBufs[0].iov_base) + DATESTARTOFF,
	      globalTimeOfDayStr, DATESTRLEN);
      c->hc_cacheEnt->ce_respHeaderTime = globalTimeOfDay.tv_sec;
    } 
  }

  if ((c->hc_method == METHOD_GET) && bytesToSend) {
    ioBufs[numIOBufs].iov_base = (char *) dataToSend + startOffset;
    ioBufs[numIOBufs].iov_len = bytesToSend;
    numIOBufs++;
  }

  sz = writev(c->hc_fd, ioBufs, numIOBufs);

  if (sz <= 0 && testing)	/* doing optimistic write */
    return;
  
  if (c->hc_bytesSent==0 && c->hc_cacheEnt->ce_respHeaderLen) {
    if (sz > 0)
      sz -= c->hc_cacheEnt->ce_respHeaderLen;
  }

  if (c->hc_method == METHOD_HEAD) {
    if (sz)
      fprintf(stderr, "had non-zero sz with head\n");
    DoneWithConnection(c, FALSE);
    return;
  }
  
  if (sz < 0) {
    /* Something went wrong, close this connection.
     **
     ** If it's just an EPIPE, don't bother logging, that
     ** just means the client hung up on us.
     **
     ** On some systems, write() occasionally gives an EINVAL.
     ** Dunno why, something to do with the socket going
     ** bad.  Anyway, we don't log those either.
     */
    if (errno != EPIPE && errno != EINVAL)
      perror("single write");
    fprintf(stderr, "errno: %d, defaultSendBufSize %d\n", 
	    errno, defaultSendBufSize);
    DoneWithConnection(c, TRUE);
    return;
  }

  /* move the chunk lock */
  if (!c->hc_numChunkLocks) {
    fprintf(stderr, "trying to move a conn without a lock\n");
    exit(-1);
  }
  if (IncChunkLock(c->hc_cacheEnt, c->hc_bytesSent + sz))
    c->hc_numChunkLocks++;
  if (DecChunkLock(c->hc_cacheEnt, c->hc_bytesSent))
    c->hc_numChunkLocks--;
  
  /* Ok, we wrote something. */
  c->hc_bytesSent += sz;
  if (c->hc_bytesSent >= c->hc_cacheEnt->ce_size) {
    /* This conection is finished! */
    DoneWithConnection(c, FALSE);
    return;
  }

  SetTimer(c, IDLEC_TIMELIMIT, IdleTimeout);	
}
/* ---------------------------------------------------------------- */
SRCode 
RegularFileStuff(httpd_conn* hc, HotNameEntry **hotName, 
		 int addToHNE, long fileSize, time_t modTime)
{    
  char *expanded;

  expanded = (*hotName)->hne_expanded;
  hc->hc_cacheEnt = CheckCache(expanded);

  if (!hc->hc_cacheEnt) {
    if (ColdFileStuff(hc, expanded, fileSize, modTime))
      return(SR_ERROR);
  }
  hc->hc_status = 200;		/* file exists, OK */
  if (IncChunkLock(hc->hc_cacheEnt, 0)) 
    hc->hc_numChunkLocks++;	/* indicate we're starting */
  
  /* since the name cache operates independently of the file cache,
     do _not_ include this check inside the file cache check, or
     else the name cache gets disabled */
  if (addToHNE) {
    hc->hc_hne = *hotName;
    EnterIntoHotNameCache(hotName);
  }
  
  /* Handle If-Modified-Since. */
  if (hc->hc_ifModifiedSince != (time_t) -1 &&
      hc->hc_ifModifiedSince == hc->hc_cacheEnt->ce_modTime) {
    SendMime(hc, 304, err304title, 
	      hc->hc_cacheEnt->ce_encodings, "", 
	      hc->hc_cacheEnt->ce_type, 
	      -1,		/* suppress length field */
	      hc->hc_cacheEnt->ce_modTime);
    return(SR_DONOTHING);
  }
  
  return(SR_READY);
}
/* ---------------------------------------------------------------- */
