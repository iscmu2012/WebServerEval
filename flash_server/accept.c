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


#include "config.h"		/* move early to define FD_SETSIZE */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <errno.h>
#include "conn.h"
#include "handy.h"
#include "timer.h"
#include "libhttpd.h"
#include "loop.h"
#include "accept.h"
#include "common.h"

static int InitializeConn(httpd_conn* hc, int cnum);
/* ---------------------------------------------------------------- */
void
PrepareConnForNextRequest(httpd_conn* hc)
{
  /* resets values of a connection - if called directly, this is being
     done for persistent connections */

  hc->hc_method = METHOD_ERROR;
  hc->hc_accept[0] = '\0';
  hc->hc_accepte[0] = '\0';
  hc->hc_status = 500;

  hc->hc_encodedurl = hc->hc_protocol = 
    hc->hc_referer = hc->hc_userAgent =
      hc->hc_cookie = hc->hc_contentType = "";
  
  hc->hc_ifModifiedSince = (time_t) -1;
  hc->hc_contentLength = -1;
  hc->hc_isPersistentConnection = FALSE;
  hc->hc_numChunkLocks = 0;
  hc->hc_stripped = NULL;
  hc->hc_bytesSent = 0;
  hc->hc_neededDiskRead = FALSE;
  hc->hc_hadMincoreMisses = FALSE;
}
/* ---------------------------------------------------------------- */
static void
PrepareConnOnAccept(httpd_conn* hc, int fd, struct sockaddr_in *sin)
{
  /* prepares the connection for the first request after the accept */

  hc->hc_fd = fd;
  hc->hc_nagleOff = FALSE;

  /* we assume that accept() inherits the O_NDELAY
     settings, so we don't need to do it ourselves
     if (fcntl(hc->fd, F_SETFL, O_NDELAY) < 0)
     syslog(LOG_ERR, "fcntl O_NDELAY - %m");
     */
  
  hc->hc_clientAddr = sin->sin_addr;
  
  PrepareConnForNextRequest(hc);
}
/* ---------------------------------------------------------------- */
static int
InitializeConn(httpd_conn* hc, int cnum)
{
  hc->hc_maxAccept = hc->hc_maxAccepte = 0;
  if (!realloc_str(&hc->hc_accept, &hc->hc_maxAccept, 0))
    return(TRUE);
  if (!realloc_str(&hc->hc_accepte, &hc->hc_maxAccepte, 0))
    return(TRUE);
  
  hc->hc_maxOrigFirstLine = hc->hc_origFirstLineLen = 0;
  hc->hc_origFirstLine = NULL;	/* alloc'd later if needed */

  hc->hc_cacheEnt = NULL;
  hc->hc_headerInfo = NULL;
  hc->hc_hne = NULL;
  hc->hc_cgiInfo = NULL;
  hc->hc_cnum = cnum;
  InitTimer(hc);
  return(FALSE);
}
/* ---------------------------------------------------------------- */
static int
AllocateNewConnEntries(int cnum)
{
  /* returns TRUE on failure */
  httpd_conn* c;
  int numEntries = 1024/sizeof(httpd_conn);
  int i;

  if (numEntries < 5)
    numEntries = 5;
  if (numEntries + cnum > maxConnects)
    numEntries = maxConnects - cnum;
  assert(numEntries);
  c = calloc(numEntries, sizeof(httpd_conn));
  if (!c) {
    fprintf(stderr, "failed to allocate new conn entries\n");
    if (!cnum) 
      exit(-1);
    /* limit the number of connections we want to handle */
    maxConnects = cnum;
    return(TRUE);
  }

  for (i = 0; i < numEntries; i++) {
    allConnects[cnum+i] = &c[i];
    if (InitializeConn(&c[i], cnum+i)) {
      fprintf(stderr, "failed initializing new conns\n");
      /* limit the number of connections we want to handle */
      if (i) {
	/* use whatever connections were initialized */
	maxConnects = cnum + i-1;
	return(FALSE);
      }
      maxConnects = cnum;
      return(TRUE);
    }
  }

  return(FALSE);
}
/* ---------------------------------------------------------------- */
int
AcceptConnections(int cnum, int acceptMany)
{
  /* returns TRUE if we need to create new threads,
     or returns FALSE for everything else. Yes, the
     return code only makes sense for the threaded
     version, but that's OK */

  httpd_conn* c;
  int newConnFD;

  /* Is there room in the connection table? */
  if (numConnects >= maxConnects) {
    fprintf(stderr, "too many conns\n");
    return(FALSE);
  }

  do {
    struct sockaddr_in sin;
    size_t sz;
    int i;
    
    sz = sizeof(sin);

    newConnFD = accept(HS.fd, (struct sockaddr*) &sin, &sz);
    if (newConnFD < 0) {
      return(FALSE);
    }

    mainStats.cs_numAccepts++;

    if (acceptMany) {
      /* find the connection number each time */
      cnum = -1;
      /* Find a free connection entry. */
      for (i = firstFreeConnHint; i < FD_SETSIZE/8; i++) {
	if (freeConnBits[i]) {
	  int j = 0;
	  int val;
	  val = freeConnBits[i];
	  while (!(val & 1)) {
	    val >>=1;
	    j++;
	  }
	  cnum = (i<<3)+j;
	  firstFreeConnHint = i;
	  /* clear the appropriate bit later, after
	     we know there are no errors with the connection */
	  break;
	}
      }
    }
     
    if (cnum == -1) {
      fprintf(stderr, "freeConnsHead is -1: this shouldn't have happened\n");
      exit(-1);
    }

    c = allConnects[cnum];

    if (!c) {
      /* allocate and initialize new connections */
      if (AllocateNewConnEntries(cnum)) {
	/* sending a 500 message at this point is hard,
	   so just skip it. We don't expect this to occur */
	close(newConnFD);
	return(FALSE);
      }
      c = allConnects[cnum];
    }
    
    /* initialize the connection before doing anything else */
    PrepareConnOnAccept(c, newConnFD, &sin);

    MakeConnFDAssociation(c, c->hc_fd);

    freeConnBits[cnum>>3] &= ~(1<<(cnum&7));
    numConnects++;		/* do this here, since we might
				   close the connection when reading */
    /* although this read might seem unnecessary because of the
       optimistic write after the regular read in the main loop, it
       seems to have a major impact on performance if it's removed */

    if (DoConnReadingBackend(c, newConnFD, TRUE) == DCR_READMORE) 
      SetSelectHandler(newConnFD, DoConnReading, SSH_READ);
  } while (numConnects < maxConnects && acceptMany);

  if (numConnects >= maxConnects) 
    DisallowNewClients();
  return(FALSE);
}
/* ---------------------------------------------------------------- */
