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
#include <assert.h>
#include "hotfile.h"
#include "conn.h"
#include "helper.h"
#include "libhttpd.h"
#include "loop.h"
#include "hotname.h"
#include "config.h"
#include "common.h"
#include "handy.h"

int numActiveDirSlaves;

#define STAGE_BUF_SIZE 2000

typedef struct DirStageBuf {
  int sb_bytesLeft;		/* # of bytes left to read from helper */
  char *sb_buf;			/* staging buffer for this conn */
  char *sb_start;		/* where new data starts in buf */
  int sb_numValid;		/* # unsent bytes in stage buf */
} DirStageBuf;

typedef struct HelperInfo {
  int hi_fd;
  httpd_conn *hi_hc;
} HelperInfo;

static HelperInfo *helperInfo;	

static httpd_conn *waitingList;

static int CurrentReaderDone(httpd_conn *hc, int fd, int forRW);

static char dirSlaveName[MAXPATHLEN];
static int HelperGoneIdle(httpd_conn *hc, int fd, int forRW);
static int ConsumeHelperStream(httpd_conn *hc, int fd, int forRW);
static int WriteStreamToClient(httpd_conn *hc, int fd, int forRW);

static struct timeval timeWaitListStarted;

static int initDirHelp = 0;

/* ---------------------------------------------------------------- */
void
InitDirSlaves(char *cwd)
{
  helperInfo = calloc(maxDirHelp, sizeof(HelperInfo));
  if (!helperInfo)
    Panic("out of memory allocating dir slaves");

  sprintf(dirSlaveName, "%s%s", cwd, "dir_slave");

  if (initDirHelp) {
    int i;
    assert(initDirHelp <= maxDirHelp);
    for (i = 0; i < initDirHelp; i++) {
      int fd;
      fd = helperInfo[i].hi_fd = CreateGenericSlave(dirSlaveName);
      if (fd >= 0) {
	numActiveDirSlaves++;
	ClearConnFDAssociation(fd);
	SetSelectHandler(fd, HelperGoneIdle, SSH_READ);
      }
      else {
	fprintf(stderr, "died in dir slave\n");
	exit(-1);
      }
    }
  }
}
/* ---------------------------------------------------------------- */
static void
AddCurrentReader(httpd_conn *hc, int pos)
{
  int res;
  char bothDirNames[3+2*MAXPATHLEN];
  
  mainStats.cs_numActualDirs++;

  sprintf(bothDirNames, "%c%s %s", 
	  (hc->hc_method == METHOD_HEAD) ? 'h' : 'g',
	  hc->hc_hne->hne_expanded, hc->hc_encodedurl);
  res = write(helperInfo[pos].hi_fd, bothDirNames, 1+strlen(bothDirNames));
  if (res !=  1+strlen(bothDirNames)) {
    fprintf(stderr, "helperInfo[pos].hi_fd %d\n", helperInfo[pos].hi_fd);
    fprintf(stderr, "couldn't write both names in dir, res %d\n", res);
    perror("dir");
    exit(-1);
  }
  helperInfo[pos].hi_hc = hc;
  MakeConnFDAssociation(hc, helperInfo[pos].hi_fd);
  SetSelectHandler(helperInfo[pos].hi_fd, CurrentReaderDone, SSH_READ);
}
/* ---------------------------------------------------------------- */
static int
MatchHelperToPos(int fd, httpd_conn *hc)
{
  int i;

  /* find which entry */
  for (i = 0; i < numActiveDirSlaves; i++) {
    if (helperInfo[i].hi_fd == fd)
      break;
  }
  if (i >= numActiveDirSlaves){
    fprintf(stderr, "weird call in MatchHelperToPos\n");
    exit(-1);
  }
  if (helperInfo[i].hi_hc != hc) {
    fprintf(stderr, "mismatch in dir - i %d, fd %d, hc 0x%lx, si 0x%lx\n",
	    i, fd, (long) hc, (long) helperInfo[i].hi_hc);
    exit(-1);
  }
  return(i);
}
/* ---------------------------------------------------------------- */
static int
MatchHelperByHC(httpd_conn *hc)
{
  int i;

  /* find which entry */
  for (i = 0; i < numActiveDirSlaves; i++) {
    if (helperInfo[i].hi_hc == hc)
      break;
  }
  if (i >= numActiveDirSlaves){
    fprintf(stderr, "weird call in MatchHelperToPos\n");
    exit(-1);
  }
  return(i);
}
/* ---------------------------------------------------------------- */
static void
KillHelperConn(int pos)
{
  close(helperInfo[pos].hi_fd);
  ClearConnFDAssociation(helperInfo[pos].hi_fd);
  SetSelectHandler(helperInfo[pos].hi_fd, NULL, SSH_NOTHING);

  /* shift what's left */
  numActiveDirSlaves--;
  for (; pos < numActiveDirSlaves; pos++) 
    helperInfo[pos] = helperInfo[pos+1];
}
/* ---------------------------------------------------------------- */
static int
HelperGoneIdle(httpd_conn *hc, int fd, int forRW)
{
  int pos;
  int replyVal;

  pos = MatchHelperToPos(fd, NULL);

  if (read(fd, &replyVal, sizeof(replyVal)) != sizeof(replyVal) ||
      replyVal != -1) {
    fprintf(stderr, "replyVal: %d\n", replyVal);
    Panic("helper returned short message");
  }

  KillHelperConn(pos);
  return(0);
}
/* ---------------------------------------------------------------- */
static void
ScheduleNextWaiter(int pos)
{
  struct httpd_conn *hc;

  helperInfo[pos].hi_hc = NULL;	/* mark empty */
  ClearConnFDAssociation(helperInfo[pos].hi_fd);
  SetSelectHandler(helperInfo[pos].hi_fd, HelperGoneIdle, SSH_READ);

  if (!waitingList)
    return;
  hc = waitingList;
  waitingList = hc->hc_next;
  hc->hc_next = NULL;
  AddCurrentReader(hc, pos);
  
  if (!waitingList)
    timeWaitListStarted.tv_sec = 0;
}
/* ---------------------------------------------------------------- */
static DirStageBuf *
AllocateStageBuf(void)
{
  DirStageBuf *temp;
  temp = calloc(1, sizeof(DirStageBuf));
  if (!temp)
    return(NULL);
  temp->sb_start = temp->sb_buf = malloc(STAGE_BUF_SIZE);
  if (temp->sb_buf)
    return(temp);
  free(temp);
  return(NULL);
}
/* ---------------------------------------------------------------- */
void
ReleaseDirStageBuf(struct DirStageBuf **dsbP)
{
  DirStageBuf *dsb;
  dsb = *dsbP;
  if (dsb->sb_buf)
    free(dsb->sb_buf);
  free(dsb);
  *dsbP = NULL;
}
/* ---------------------------------------------------------------- */
static int
WriteStreamToClient(httpd_conn *hc, int fd, int forRW)
{
  /* send to client, repeat as needed, continue reading if needed */
  int bytes;

  /* the fd here should be the client's fd */
  assert(fd == hc->hc_fd);

  bytes = write(hc->hc_fd, hc->hc_dsb->sb_start, hc->hc_dsb->sb_numValid);
  if (bytes < 1) {
    fprintf(stderr, "weird write helper\n");
    if (hc->hc_dsb->sb_bytesLeft) {
      /* the client is weird, and we've got a helper waiting */
      int pos;
      pos = MatchHelperByHC(hc);
      KillHelperConn(pos);
    }
    DoneWithConnection(hc, TRUE);
    return(0);
  }

  hc->hc_bytesSent += bytes;
  hc->hc_dsb->sb_start += bytes;
  hc->hc_dsb->sb_numValid -= bytes;
  if (hc->hc_dsb->sb_numValid) /* continue waiting on write */
    return(0);

  if (hc->hc_dsb->sb_bytesLeft) { /* need to read more from helper */
    int pos;
    pos = MatchHelperByHC(hc);
    /* deactivate waiting on client */
    SetSelectHandler(fd, NULL, SSH_NOTHING);
    /* wait on read from helper */
    SetSelectHandler(helperInfo[pos].hi_fd, ConsumeHelperStream, SSH_READ);
    return(0);
  }

  /* we're done with this transfer - next waiter was already
     scheduled when we finished reading from helper */
  DoneWithConnection(hc, FALSE);
  return(0);
}
/* ---------------------------------------------------------------- */
static int
ConsumeHelperStream(httpd_conn *hc, int fd, int forRW)
{
  /* read from helper, then wait for client to be ready */
  int bytes;
  int pos;

  pos = MatchHelperToPos(fd, hc);
  bytes = (hc->hc_dsb->sb_bytesLeft < STAGE_BUF_SIZE) ?
    hc->hc_dsb->sb_bytesLeft : STAGE_BUF_SIZE;
  assert(bytes);
  bytes = read(fd, hc->hc_dsb->sb_buf, bytes);
  if (bytes < 1) {
    fprintf(stderr, "weird read from helper\n");
    KillHelperConn(pos);
    DoneWithConnection(hc, TRUE);
    return(0);
  }

  hc->hc_dsb->sb_start = hc->hc_dsb->sb_buf;
  hc->hc_dsb->sb_numValid = bytes;
  hc->hc_dsb->sb_bytesLeft -= bytes;

  /* wait on client's buffers */
  SetSelectHandler(hc->hc_fd, WriteStreamToClient, SSH_WRITE);
  /* ignore helper for the time being */
  SetSelectHandler(fd, NULL, SSH_NOTHING);

  /* if we're done with helper, allow it to proceed */
  if (!hc->hc_dsb->sb_bytesLeft)
    ScheduleNextWaiter(pos);
  
  return(0);
}
/* ---------------------------------------------------------------- */
static int
CurrentReaderDone(httpd_conn *hc, int fd, int forRW)
{
  int bytes;
  int pos;
  int replyVal;

  pos = MatchHelperToPos(fd, hc);
  bytes = read(fd, &replyVal, sizeof(replyVal));
  if (bytes < sizeof(replyVal)) {
    fprintf(stderr, "dir had error?\n");
    exit(-1);
  }

  if (replyVal < 0) {
    /* crossed wires - ignore */
    AddCurrentReader(hc, pos);
    return(0);
  }

  if (replyVal == 0) {
    /* problem opening directory - error */
    HttpdSendErr(hc, 404, err404title, err404form, hc->hc_encodedurl);
    DoneWithConnection(hc, TRUE);
    ScheduleNextWaiter(pos);
    return(0);
  }

  if (hc->hc_method == METHOD_HEAD) {
    /* there's no data coming, but the
       positive reply means that everything's OK */
    SendMime(hc, 200, ok200title, "", "", "text/html", 
	     -1, hc->hc_hne->hne_modTime);
    DoneWithConnection(hc, TRUE);
    ScheduleNextWaiter(pos);
    return(0);
  }

  /* we have a real directory listing coming back - 
     make space and start consuming */
  if (hc->hc_dsb)
    ReleaseDirStageBuf(&hc->hc_dsb);
  hc->hc_dsb = AllocateStageBuf();
  if (!hc->hc_dsb) {
    fprintf(stderr, "failed to get staging buf\n");
    HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    DoneWithConnection(hc, TRUE);
    ScheduleNextWaiter(pos);
    return(0);
  }
  hc->hc_dsb->sb_bytesLeft = replyVal;

  /* send reply header first */
  SendMime(hc, 200, ok200title, "", "", "text/html", 
	   replyVal, hc->hc_hne->hne_modTime);

  /* start reading from helper */
  MakeConnFDAssociation(hc, fd);
  SetSelectHandler(fd, ConsumeHelperStream, SSH_READ);
  return(0);
}
/* ---------------------------------------------------------------- */
void
ScheduleDirHelp(struct httpd_conn *hc)
{
  int i;
  httpd_conn *tail = NULL;
  int newSlave;

  SetSelectHandler(hc->hc_fd, NULL, SSH_NOTHING);

  hc->hc_next = NULL;
  hc->hc_siblings = NULL;

  /* since the helper actually produces data, do not
     allow any combining while waiting - otherwise,
     we'd have a single-producer/multiple-consumder
     problem when the helper is finished */

  /* get free spot if one's open */
  for (i = 0; i < numActiveDirSlaves; i++) {
    if (!helperInfo[i].hi_hc) {
      AddCurrentReader(hc, i);
      return;
    }
  }

  /* create a new slave if we've got free spots 
     and if the timing works */

  newSlave = FALSE;
  if (!numActiveDirSlaves) 
    newSlave = TRUE;
  else if (numActiveDirSlaves >= maxDirHelp)
    newSlave = FALSE;		/* basically a no-op, but easy to read */
  else if (slaveDelayTime) {
    if (timeWaitListStarted.tv_sec &&
	(DiffTime(&timeWaitListStarted, &globalTimeOfDay) >= slaveDelayTime))
    newSlave = TRUE;
  }
  else
    newSlave = TRUE;

  if (newSlave) {
    timeWaitListStarted = globalTimeOfDay;

    i = numActiveDirSlaves;
    helperInfo[i].hi_fd = CreateGenericSlave(dirSlaveName);
    if (helperInfo[i].hi_fd >= 0) {
      numActiveDirSlaves++;
      AddCurrentReader(hc, i);
      return;
    }
    else {
      fprintf(stderr, "failed in lazy fork for %s\n", dirSlaveName);
      if (!numActiveDirSlaves)
	exit(-1);
    }
  }

  tail = waitingList;
  while (tail && tail->hc_next)
    tail = tail->hc_next;

  /* go to end of list */
  if (tail)
    tail->hc_next = hc;
  else {
    waitingList = hc;
    timeWaitListStarted = globalTimeOfDay;
  }
}
/* ---------------------------------------------------------------- */
