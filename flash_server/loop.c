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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/resource.h>
#include "libhttpd.h"
#include "loop.h"
#include "conn.h"
#include "timer.h"
#include "accept.h"
#include "hotname.h"
#include "hotfile.h"
#include "cold.h"
#include "readreq.h"
#include "datacache.h"
#include "cgi.h"
#include "helper.h"
#include "common.h"
#include "handy.h"

static int highestFDInvolvedWithRW = -1;
SelectHandler allHandlers[FD_SETSIZE];
static fd_set masterReadFDSet;
static fd_set masterWriteFDSet;
char freeConnBits[FD_SETSIZE/8];
int firstFreeConnHint = 0;
int numConnects;

static int newClientsDisallowed;

static void FlushAccessLog(void);

/* ---------------------------------------------------------------- */
void
InitFDSets(void)
{
  FD_ZERO(&masterReadFDSet);
  FD_ZERO(&masterWriteFDSet);
}
/* ---------------------------------------------------------------- */
ConnStats mainStats;
PermStats permStats;
static void
PrintMainStats(void)
{
  double temp;
  double temp2;
  struct rusage usageInfo;

  if (doQueueLenDumps) {
    fprintf(stderr, "\n");
    DumpDataCacheStats();
    DumpHotNameStats();
  }

  if (!doMainStats)
    return;

  fprintf(stderr, "\n");
  fprintf(stderr, "accepts: %d\n", mainStats.cs_numAccepts);
  fprintf(stderr, "requests: %d\n", mainStats.cs_numRequests);
  fprintf(stderr, "reqs needing reads: %d\n", 
	  mainStats.cs_numReqsNeedingReads);
  fprintf(stderr, "reqs w/ mincore misses: %d\n", 
	  mainStats.cs_numReqsWithMincoreMisses);
  fprintf(stderr, "data bytes sent: %.0f\n", mainStats.cs_bytesSent);
  fprintf(stderr, "data pages sent: %.0f\n", mainStats.cs_numPagesSent);
  fprintf(stderr, "bytes per accept: %.1f\n", (mainStats.cs_numAccepts) ?
	  mainStats.cs_bytesSent/mainStats.cs_numAccepts : 0);
  fprintf(stderr, "selects: %.0f\n", mainStats.cs_numSelects);
  fprintf(stderr, "items per select: %.2f\n", (mainStats.cs_numSelects) ?
	  mainStats.cs_totSelectsReady/mainStats.cs_numSelects : 0);
  temp2 = temp = 0;
  if (mainStats.cs_numDataCacheChecks) {
    temp = (100.0 * mainStats.cs_numDataCacheHits)/
      mainStats.cs_numDataCacheChecks;
    temp2 = mainStats.cs_numDataCacheWalks / 
      (double) mainStats.cs_numDataCacheChecks;
  }
  fprintf(stderr, "num entries in data cache: %d\n", 
	  permStats.ps_numDataCacheEntries);
  fprintf(stderr, "data cache hit rate: %.2f%% (%d/%d)\n",
	  temp, mainStats.cs_numDataCacheHits, 
	  mainStats.cs_numDataCacheChecks);
  fprintf(stderr, "data cache walk len: %.2f\n", temp2);
  temp2 = temp = 0;
  if (mainStats.cs_numHotNameChecks) {
    temp = (100.0 * mainStats.cs_numHotNameHits)/mainStats.cs_numHotNameChecks;
    temp2 = mainStats.cs_numHotNameWalks / 
      (double) mainStats.cs_numHotNameChecks;
  }
  fprintf(stderr, "hot name hit rate: %.2f%% (%d/%d)\n",
	  temp, mainStats.cs_numHotNameHits, mainStats.cs_numHotNameChecks);
  fprintf(stderr, "hot name walk len: %.2f\n", temp2);
  fprintf(stderr, "converts: %d\n", mainStats.cs_numConvBlocks);
  fprintf(stderr, "real convs: %d\n", mainStats.cs_numActualConvs);
  fprintf(stderr, "reads: %d\n", mainStats.cs_numReadBlocks);
  fprintf(stderr, "real good reads: %d\n", mainStats.cs_numActualReads);
  fprintf(stderr, "failed reads: %d\n", mainStats.cs_numReadsFailed);
  fprintf(stderr, "pages in real reads: %d\n", mainStats.cs_totRealReadPages);
  fprintf(stderr, "bytes in real reads: %.0f\n", 
	  mainStats.cs_totRealReadBytes);
  fprintf(stderr, "read helpers: %d\n", numActiveReadSlaves);
  fprintf(stderr, "pathname conversion helpers: %d\n", numActiveConvSlaves);
  fprintf(stderr, "ls helpers: %d\n", numActiveDirSlaves);
  memset(&mainStats, 0, sizeof(mainStats));
  
  if (getrusage(RUSAGE_SELF, &usageInfo) < 0) {
    fprintf(stderr, "couldn't get resource usage info\n");
    return;
  }
  
  usageInfo.ru_utime.tv_sec -= permStats.ps_lastUsage.ru_utime.tv_sec;
  usageInfo.ru_utime.tv_usec -= permStats.ps_lastUsage.ru_utime.tv_usec;
  usageInfo.ru_stime.tv_sec -= permStats.ps_lastUsage.ru_stime.tv_sec;
  usageInfo.ru_stime.tv_usec -= permStats.ps_lastUsage.ru_stime.tv_usec;
  /* usageInfo.ru_maxrss; */
  usageInfo.ru_idrss -= permStats.ps_lastUsage.ru_idrss;
  usageInfo.ru_minflt -= permStats.ps_lastUsage.ru_minflt;
  usageInfo.ru_majflt -= permStats.ps_lastUsage.ru_majflt;
  usageInfo.ru_nswap -= permStats.ps_lastUsage.ru_nswap;
  usageInfo.ru_inblock -= permStats.ps_lastUsage.ru_inblock;
  usageInfo.ru_oublock -= permStats.ps_lastUsage.ru_oublock;
  usageInfo.ru_msgsnd -= permStats.ps_lastUsage.ru_msgsnd;
  usageInfo.ru_msgrcv -= permStats.ps_lastUsage.ru_msgrcv;
  usageInfo.ru_nsignals -= permStats.ps_lastUsage.ru_nsignals;
  usageInfo.ru_nvcsw -= permStats.ps_lastUsage.ru_nvcsw;
  usageInfo.ru_nivcsw -= permStats.ps_lastUsage.ru_nivcsw;

  /* don't worry about negative values in tv_usec fields */

  fprintf(stderr, "usageInfo.ru_utime is %.3f\n", 
	  usageInfo.ru_utime.tv_sec + 1e-6 * usageInfo.ru_utime.tv_usec);
  fprintf(stderr, "usageInfo.ru_stime is %.3f\n", 
	  usageInfo.ru_stime.tv_sec + 1e-6 * usageInfo.ru_stime.tv_usec);
  fprintf(stderr, "usageInfo.ru_maxrss is %ld\n", usageInfo.ru_maxrss);
  fprintf(stderr, "usageInfo.ru_idrss is %ld\n", usageInfo.ru_idrss);
  fprintf(stderr, "usageInfo.ru_minflt is %ld\n", usageInfo.ru_minflt);
  fprintf(stderr, "usageInfo.ru_majflt is %ld\n", usageInfo.ru_majflt);
  fprintf(stderr, "usageInfo.ru_nswap is %ld\n", usageInfo.ru_nswap);
  fprintf(stderr, "usageInfo.ru_inblock is %ld\n", usageInfo.ru_inblock);
  fprintf(stderr, "usageInfo.ru_oublock is %ld\n", usageInfo.ru_oublock);
  fprintf(stderr, "usageInfo.ru_msgsnd is %ld\n", usageInfo.ru_msgsnd);
  fprintf(stderr, "usageInfo.ru_msgrcv is %ld\n", usageInfo.ru_msgrcv);
  fprintf(stderr, "usageInfo.ru_nsignals is %ld\n", usageInfo.ru_nsignals);
  fprintf(stderr, "usageInfo.ru_nvcsw is %ld\n", usageInfo.ru_nvcsw);
  fprintf(stderr, "usageInfo.ru_nivcsw is %ld\n", usageInfo.ru_nivcsw);

  /* now really store the value */
  if (getrusage(RUSAGE_SELF, &permStats.ps_lastUsage) < 0) {
    fprintf(stderr, "couldn't get resource usage info\n");
    return;
  }

}
/* ---------------------------------------------------------------- */
void
DisallowNewClients(void)
{
  /* if we get too many connections,
     we turn off checking for new requests */
  FD_CLR(HS.fd, &masterReadFDSet);
  newClientsDisallowed = TRUE;
  fprintf(stderr, "new connects disallowed\n");
}
/* ---------------------------------------------------------------- */
static void
ReenableNewClients(void)
{
  FD_SET(HS.fd, &masterReadFDSet);
  newClientsDisallowed = FALSE;

  fprintf(stderr, "new connects re-enabled\n");
}
/* ---------------------------------------------------------------- */
void 
MainLoop(void)
{
  fd_set rfdset;
  fd_set wfdset;
  int r;
  struct timeval selectTimeout;
  int i;
  long lastRealSelectTime = 0;
  time_t globalTimeStrVal = 0;

  /* figure out all resource usage until this point */
  if (getrusage(RUSAGE_SELF, &permStats.ps_lastUsage) < 0) 
    fprintf(stderr, "couldn't get resource usage info\n");

  FD_SET(HS.fd, &masterReadFDSet);

  selectTimeout.tv_sec = 1;
  selectTimeout.tv_usec = 0;
  if (highestFDInvolvedWithRW < HS.fd)
    highestFDInvolvedWithRW = HS.fd;

  numConnects = 0;

  /* Main loop. */
  for (;;) {

    /* check timers _before_ copying rd/wr sets, 
       since a connection timing out shouldn't
       be included in the sets sent to select */
    if (lastTimerCheckTime != globalTimeOfDay.tv_sec)
      CheckTimers();

    /* Set up the fdsets. */
    memcpy(&rfdset, &masterReadFDSet, sizeof(rfdset));
    memcpy(&wfdset, &masterWriteFDSet, sizeof(wfdset));

#ifdef __linux__
    selectTimeout.tv_sec = 1;
    selectTimeout.tv_usec = 0;
#endif

    /* Do the select. */
    r = select(highestFDInvolvedWithRW+1, 
	       &rfdset, &wfdset, NULL, &selectTimeout);

    gettimeofday(&globalTimeOfDay, (struct timezone*) 0);
    if (globalTimeOfDay.tv_sec != globalTimeStrVal) {
      globalTimeStrVal = globalTimeOfDay.tv_sec;
      MakeHTTPDate(globalTimeOfDayStr, globalTimeStrVal);
      if (accessLoggingEnabled)
	FlushAccessLog();
    }

    if (r < 0) {
      if (errno == EINTR)
	continue;	/* try again */
      perror("select");
      fprintf(stderr, "select %d\n", errno);
      exit(1);
    }

    if (r)
      lastRealSelectTime = globalTimeOfDay.tv_sec;
    
    if (r == 0) {
      if (lastRealSelectTime && (globalTimeOfDay.tv_sec - lastRealSelectTime > 2)) {
	PrintMainStats();
	lastRealSelectTime = 0;
      }
      continue;			/* No fd's are ready - run the timers. */
    }

    mainStats.cs_numSelects++;
    mainStats.cs_totSelectsReady += r;

    if (FD_ISSET(HS.fd, &rfdset)) {
      r--;
      FD_CLR(HS.fd, &rfdset);
    }
    
    /* If we get here there may be writable file descriptors for one or
     ** more connections.  Find 'em and service 'em.
     */

    for (i = 0; r && i <= highestFDInvolvedWithRW; i++) {
      int doRead, doWrite;

      doRead = FD_ISSET(i, &rfdset);
      doWrite = FD_ISSET(i, &wfdset);
      

      if (doRead || doWrite) {
	int cnum;
	SelectHandler handler;
	httpd_conn *tempConn;

	cnum = fdToConnMap[i];
	handler = allHandlers[i];
	if (handler == NULL) {
	  fprintf(stderr, "got badness in rw loop - conn %d\n", i);
	  exit(-1);
	}
	if (doRead)
	  r--;
	if (doWrite)
	  r--;
	if (cnum < 0)
	  tempConn = NULL;
	else
	  tempConn = allConnects[cnum];
	handler(tempConn, i, 
		((doRead) ? SSH_READ : 0) | ((doWrite) ? SSH_WRITE : 0));
      }
    }

    if (r) {
      fprintf(stderr, "have %d connections not serviced\n", r);
      exit(-1);
    }

    if (!newClientsDisallowed) {
      AcceptConnections(-1, TRUE);
    }
  }
}
/* ---------------------------------------------------------------- */
static SRCode
StartRequest(httpd_conn* hc)
{
  SRCode r;
  
  /* if we don't recognize the request method, it's an error */
  if (hc->hc_method == METHOD_ERROR) {
    HttpdSendErr(hc, 501, err501title, err501form, 
		 methodStrings[hc->hc_method]);
    return(SR_ERROR);
  }

  hc->hc_hne = FindMatchInHotNameCache(hc->hc_encodedurl);

  if (hc->hc_hne) {
    switch(hc->hc_hne->hne_type) {
    case HNT_FILE:
      r = RegularFileStuff(hc, &hc->hc_hne, FALSE, 0, 0);
      break;
    case HNT_REDIRECT:
      SendDirRedirect(hc);
      r = SR_ERROR;
      break;
    case HNT_LS:
      ScheduleDirHelp(hc);
      return(SR_CGI);
      break;
    default:
      fprintf(stderr, "got a weird code in switch on hne\n");
      exit(-1);
      break;
    };
  }
  else
    r = ProcessColdRequest(hc);
  
  return r;
}
/* ---------------------------------------------------------------- */
int 
DoConnReading(httpd_conn* c, int fd, int forRW)
{
  /* xxx note: doing an optimistic write after the DoConnReading seems
     to hurt degrade the performance of persistent connects on small
     files. I'm not sure why. */

  if (DoConnReadingBackend(c, fd, TRUE) == DCR_READYFORWRITE) {
    DoSingleWriteBackend(c, fd, TRUE);
  }

  return(0);
}
/* ---------------------------------------------------------------- */
void
ResumeAfterConnReading(httpd_conn* c)
{
  /* this function picks up right after the conn reading
     is done - it's used to continue processing after
     name conversion has taken place */

  if (DoConnReadingBackend(c, c->hc_fd, FALSE) == DCR_READYFORWRITE) {
    DoSingleWriteBackend(c, c->hc_fd, TRUE);
  }
}
/* ---------------------------------------------------------------- */
DCRCode 
DoConnReadingBackend(httpd_conn* c, int fd, int doReqReading)
{
  /* this function tries to read the request either when a connection
     is first opened, or whenever the connection is available for
     reading. It returns DCR_READMORE if more data needs to be
     read. It returns DCR_READYFORWRITE if it's set the connection
     state to CS_SENDING. If it's closed the connection, the state should
     have been set by clear_connection, and the function returns DCR_CLOSED
     */

  SRCode srRes;

  if (doReqReading) {
    switch(ProcessRequestReading(c)) {
    case PRR_DONE:
      mainStats.cs_numRequests++;
      break;			/* switch connection to sending */
    case PRR_READMORE:
      SetTimer(c, IDLEC_TIMELIMIT, IdleTimeout);
      return(DCR_READMORE);	/* keep in read state */
      break;
    case PRR_CLOSED:		/* not sure what we should do on close */
    case PRR_ERROR:
      DoneWithConnection(c, TRUE);
      return(DCR_CLOSED);
      break;
    default:
      fprintf(stderr, "got weird PRR_\n");
      exit(-1);
      break;
    }
  }

  SetSelectHandler(fd, DoSingleWrite, SSH_WRITE);

  srRes = StartRequest(c);
  
  if (srRes == SR_ERROR) {
    /* Something went wrong.  Close down the connection. */
    DoneWithConnection(c, TRUE);
    return(DCR_CLOSED);
  }

  if (srRes == SR_CGI) {
    /* we've gotten a script - temporarily disable the client's
       socket and let the cgi-handling code take over. At some
       point, the code will finish and re-enable the socket */
    SetSelectHandler(fd, NULL, SSH_NOTHING);
    return(DCR_PRIVATE);	/* requestor does nothing */
  }
  
  if (srRes == SR_DONOTHING) {
    /* It's all handled.  Close down the connection. */
    DoneWithConnection(c, FALSE);
    return(DCR_CLOSED);
  }
  /* Cool, we have a valid connection and a file to send to it.
   ** Make an idle timer for it.
   */
  SetTimer(c, IDLEC_TIMELIMIT, IdleTimeout);
  
  c->hc_sndbuf = defaultSendBufSize; 

  return(DCR_READYFORWRITE);
}
/* ---------------------------------------------------------------- */
static int accessLogFD;
static char *resp304 = "\" 304 -\n"; /* access log line for 304 */
static int resp304Len;
typedef struct {
  struct in_addr ic_addr;
  char *ic_textName;
  int ic_textNameLen;
} InAddrCacheElem;

static InAddrCacheElem inaddrCache[256];
#define LOG_HOLD_BUF_SIZE 8192
static char logHoldBuf[LOG_HOLD_BUF_SIZE];
static int logHoldBufUsed = 0;
/* ---------------------------------------------------------------- */
void
SetUpAccessLogging(char *accessLogName)
{
  if (!strlen(accessLogName))
    return;
  if (!strcmp(accessLogName, "-"))
    return;

  accessLogFD = open(accessLogName, O_WRONLY | O_CREAT | O_APPEND, 
		     S_IRUSR | S_IWUSR);
  if (accessLogFD < 0)
    Panic("accessLogFD");
  accessLoggingEnabled = TRUE;
  resp304Len = strlen(resp304);
}
/* ---------------------------------------------------------------- */
static void
FlushAccessLog(void)
{
  if (!logHoldBufUsed)
    return;
  write(accessLogFD, logHoldBuf, logHoldBufUsed);
  logHoldBufUsed = 0;
}
/* ---------------------------------------------------------------- */
static void
DoAccessLogging(struct httpd_conn *c)
{
  static char tstr[100];
  static int tstrLen;
  static time_t tt = 0;
  int temp;

  /* calculate the time only if needed */
  if (tt != globalTimeOfDay.tv_sec) {
    struct tm gmt;
    struct tm *t;
    int days, hours, timz;
    char sign;

    /* much of this code is taken from mod_log_config.c in Apache */
    tt = globalTimeOfDay.tv_sec;

    /* Assume we are never more than 24 hours away. */
    gmt = *gmtime(&tt);	/* remember gmtime/localtime return ptr to static */
    t = localtime(&tt);	/* buffer... so be careful */
    days = t->tm_yday - gmt.tm_yday;
    hours = ((days < -1 ? 24 : 1 < days ? -24 : days * 24)
	     + t->tm_hour - gmt.tm_hour);
    timz = hours * 60 + t->tm_min - gmt.tm_min;
    sign = (timz < 0 ? '-' : '+');
    if (timz < 0) 
      timz = -timz;

    strftime(tstr, sizeof(tstr) - 10, " - - [%d/%b/%Y:%H:%M:%S ", t);
    sprintf(tstr + strlen(tstr),
	    "%c%.2d%.2d] \"", sign, timz / 60, timz % 60);
    tstrLen = strlen(tstr);
  }

  /* check, update inaddr cache if needed */
  temp = *((int *) (&c->hc_clientAddr));
  temp += temp >> 16;
  temp += temp >> 8;
  temp &= 0xff;
  if (memcmp(&inaddrCache[temp].ic_addr, &c->hc_clientAddr, 
	     sizeof(struct in_addr))) {
    inaddrCache[temp].ic_addr = c->hc_clientAddr;
    if (inaddrCache[temp].ic_textName)
      free(inaddrCache[temp].ic_textName);
    inaddrCache[temp].ic_textName = strdup(inet_ntoa(c->hc_clientAddr));
    if (!inaddrCache[temp].ic_textName)
      Panic("strdup failed");
    inaddrCache[temp].ic_textNameLen = 
      strlen(inaddrCache[temp].ic_textName);
  }

  /* flush buffer if we're out of space */
  if (logHoldBufUsed + inaddrCache[temp].ic_textNameLen + 
      c->hc_origFirstLineLen + tstrLen + 40 > LOG_HOLD_BUF_SIZE) {
    if (!logHoldBufUsed) {
      /* this line is too long to be logged - ignore */
      fprintf(stderr, "log line too long\n");
      return;
    }
    FlushAccessLog();
 }

  /* write data into our own buffer */
  memcpy(logHoldBuf+logHoldBufUsed, inaddrCache[temp].ic_textName,
	 inaddrCache[temp].ic_textNameLen);
  logHoldBufUsed += inaddrCache[temp].ic_textNameLen;
  memcpy(logHoldBuf+logHoldBufUsed, tstr, tstrLen);
  logHoldBufUsed += tstrLen;
  memcpy(logHoldBuf+logHoldBufUsed, c->hc_origFirstLine, 
	 c->hc_origFirstLineLen);
  logHoldBufUsed += c->hc_origFirstLineLen;

  if (c->hc_status == 200 && c->hc_cacheEnt &&
      c->hc_bytesSent == c->hc_cacheEnt->ce_size &&
      c->hc_cacheEnt->ce_200Resp) {
    memcpy(logHoldBuf+logHoldBufUsed, c->hc_cacheEnt->ce_200Resp,
	   c->hc_cacheEnt->ce_200RespLen);
    logHoldBufUsed += c->hc_cacheEnt->ce_200RespLen;
  }
  else if (c->hc_status == 304) {
    memcpy(logHoldBuf+logHoldBufUsed, resp304, resp304Len);
    logHoldBufUsed += resp304Len;
  }
  else {
    sprintf(logHoldBuf+logHoldBufUsed, "\" %d %d\n", c->hc_status,
	    (int) c->hc_bytesSent);
    logHoldBufUsed += strlen(logHoldBuf+logHoldBufUsed);
  }
}
/* ---------------------------------------------------------------- */
void 
DoneWithConnection(struct httpd_conn* c, int forceClose) 
{
  int cnum;
  int closeConn;

  if (c->hc_fd == -1)		/* connection already closed */
    return;

  if (accessLoggingEnabled)
    DoAccessLogging(c);

  mainStats.cs_bytesSent += c->hc_bytesSent;

  if (c->hc_neededDiskRead)
    mainStats.cs_numReqsNeedingReads++;
  if (c->hc_hadMincoreMisses)
    mainStats.cs_numReqsWithMincoreMisses++;

  if (c->hc_status == 200 && c->hc_cacheEnt)
    mainStats.cs_numPagesSent += 
      (c->hc_bytesSent + systemPageSize-1) >> systemPageSizeBits;

  closeConn = (c->hc_isPersistentConnection && (!forceClose)) ?
    FALSE : TRUE;

  cnum = c->hc_cnum;

  if (closeConn) {
    SetSelectHandler(c->hc_fd, NULL, SSH_NOTHING);
    ClearConnFDAssociation(c->hc_fd);
    if (forceClose)
      fdToConnMap[c->hc_fd] = -3;
    if (close(c->hc_fd))
      perror("close");
    c->hc_fd = -1;

    freeConnBits[cnum>>3] |= (1<<(cnum&7));
    if ((cnum>>3) < firstFreeConnHint)
      firstFreeConnHint = cnum>>3;

    ClearTimer(c);
    
    numConnects--;

    if (newClientsDisallowed)
      ReenableNewClients();
  }
  else {
    SetSelectHandler(c->hc_fd, DoConnReading, SSH_READ);

    SetTimer(c, IDLEC_TIMELIMIT, IdleTimeout);	
  }

  if (c->hc_hne) 
    ReleaseHNE(&c->hc_hne);

  /* do this before releasing cache entry */
  if (c->hc_numChunkLocks) {
    if (DecChunkLock(c->hc_cacheEnt, c->hc_bytesSent))
      c->hc_numChunkLocks--;
    if (c->hc_numChunkLocks) {
      fprintf(stderr, "still have chunk locks\n");
      exit(-1);
    }
  }

  if (c->hc_cacheEnt) 
    ReleaseCacheEntry(&c->hc_cacheEnt);

  if (c->hc_headerInfo) 
    ReleaseHeaderInfo(&c->hc_headerInfo, closeConn);

  if (c->hc_cgiInfo)
    ReleaseCGIInfo(&c->hc_cgiInfo);

  if (c->hc_dsb)
    ReleaseDirStageBuf(&c->hc_dsb);

  if (c->hc_stripped) {
    free(c->hc_stripped);
    c->hc_stripped = NULL;
  }

  if (!closeConn)
    PrepareConnForNextRequest(c);
}
/* ---------------------------------------------------------------- */
void 
SetSelectHandler(int fd, SelectHandler s, int forRW)
{
  static int needEvalHighestFD = 0;
  int i;

  if (fd < 0) {
    fprintf(stderr, "negative fd in ssh\n");
    exit(-1);
  }

  if (fd >= FD_SETSIZE) {
    fprintf(stderr, "large fd in ssh\n");
    exit(-1);
  }

  if (forRW && (s == NULL)) {
    fprintf(stderr, "unset s in ssh\n");
    exit(-1);
  }

  allHandlers[fd] = s;

  if (forRW & SSH_READ)
    FD_SET(fd, &masterReadFDSet);
  else
    FD_CLR(fd, &masterReadFDSet);

  if (forRW & SSH_WRITE)
    FD_SET(fd, &masterWriteFDSet);
  else
    FD_CLR(fd, &masterWriteFDSet);
 
  if (fd >= highestFDInvolvedWithRW && forRW) {
    highestFDInvolvedWithRW = fd;
    needEvalHighestFD = 0;
  }
  else if (fd == highestFDInvolvedWithRW && (!forRW))
    needEvalHighestFD = 1;
  else if (needEvalHighestFD)
    needEvalHighestFD++;

  if (needEvalHighestFD > 256)	{ /* some large number */
    needEvalHighestFD = 0;
    /* walk backwards, find new max */
    for (i = highestFDInvolvedWithRW; i >= 0; i--) {
      if (FD_ISSET(i, &masterReadFDSet) || FD_ISSET(i, &masterWriteFDSet)) {
	highestFDInvolvedWithRW = i;
	break;
      }
    }
    if (i == -1) 
      Panic("highestFDInvolvedWithRW");
  }

}
/* ---------------------------------------------------------------- */
void
IdleTimeout(httpd_conn* c)
{
  if (!ISCONNFREE(c->hc_cnum)) {
    fprintf(stderr, "%.80s connection timed out", 
	    inet_ntoa(c->hc_clientAddr));
    gettimeofday(&globalTimeOfDay, (struct timezone*) 0);
    DoneWithConnection(c, TRUE);
  }
}
/* ---------------------------------------------------------------- */
