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
#include "version.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>	/* for tcp_nodelay option */

#include <errno.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TIME_WITH_SYS_TIME
#include <time.h>
#endif
#include <unistd.h>
#include <sys/uio.h>

#include "datacache.h"
#include "libhttpd.h"
#include "hotname.h"
#include "readreq.h"
#include "cgi.h"
#include "conn.h"
#include "loop.h"
#include "helper.h"
#include "timer.h"
#include "handy.h"
#include "common.h"

int systemPageSize;
int systemPageSizeBits;

static char *lruCacheMBStr = "32"; /* see explanation in config.h */
int maxLRUCacheSizeBytes;

static char *maxReadHelpStr = "4"; /* see config.h */
int maxReadHelp;

static char *maxConvHelpStr = "2"; /* see config.h */
int maxConvHelp;

static char *maxDirHelpStr = "1"; /* see config.h */
int maxDirHelp;

static char *initReadHelpStr = "1"; /* see config.h */
int initReadHelp;

static char *initConvHelpStr = "1"; /* see config.h */
int initConvHelp;

static char *sendBufKBStr = "32"; /* see config.h */
int sendBufSizeBytes;

#ifdef __FreeBSD__
static char *maxSlaveIdleTimeStr = "0";	/* see config.h */
#else
static char *maxSlaveIdleTimeStr = "30"; /* see config.h */
#endif
int maxSlaveIdleTime;

static char *bindPortStr = DEFAULT_PORTSTR;

static char *docDirStr = NULL;

#ifdef ALWAYS_CHROOT
static char *doChrootStr = "1";
#else
static char *doChrootStr = "0";
#endif

static char *serverUserStr = DEFAULT_USER;

#ifdef CGI_PATTERN
static char *cgiPatternStr = CGI_PATTERN;
#else
static char *cgiPatternStr = NULL;
#endif

static char *hostnameStr = NULL;

static char *slaveDelayTimeStr = "1"; /* see config.h */
int slaveDelayTime;

static char *doQueueLenDumpsStr = "0"; /* see config.h */
int doQueueLenDumps;

static char *doMainStatsStr = "0"; /* see config.h */
int doMainStats;

static char *doMincoreDumpStr = "0"; /* see config.h */
int doMincoreDump;

static char *nameCacheSizeStr = "6000";	/* see config.h */
int maxNameCacheSize;

static char *accessLogName = "-";
int accessLoggingEnabled;

static char *numFlashProcsStr = "1"; /* number of main processes */
static int numFlashProcs;

StringOptions stringOptions[] = {
  {"-lrusize", &lruCacheMBStr, "if nonzero, heuristic LRU data cache size in MB"},
  {"-readhelp", &maxReadHelpStr, "max # of read helper processes per main proc"},
  {"-convhelp", &maxConvHelpStr, "max # of pathname conversion helper processes per main proc"},
  {"-dirhelp", &maxDirHelpStr, "max # of 'ls' helper processes per main proc"},
  {"-readinit", &initReadHelpStr,  "initial # of read helper processes per main proc"},
  {"-convinit", &initConvHelpStr, "initial # of pathname conversion helper processes per main proc"},
  {"-sendkb", &sendBufKBStr, "size in kB of per-connection send buffer"},
  {"-port", &bindPortStr, "port number on which to receive requests"},
  {"-p", &bindPortStr, "same as -port"},
  {"-dir", &docDirStr, "directory containing documents to be served"},
  {"-d", &docDirStr, "same as -dir"},
  {"-slavedel", &slaveDelayTimeStr, "seconds for slave queue to be busy before new slave starts"},
  {"-doqlen", &doQueueLenDumpsStr, "if set, shows hash bin lengths with stats"},
  {"-stats", &doMainStatsStr, "show recent statistics when server is idle"},
  {"-mincoredump", &doMincoreDumpStr, "show when mincore says page missing"},
  {"-namecache", &nameCacheSizeStr, "max entries in name cache"},
  {"-accesslog", &accessLogName, "access log file's name, or dash for none"},
  {"-helperidle", &maxSlaveIdleTimeStr, "if set, max idle time for helper"},
  {"-flashprocs", &numFlashProcsStr, "# of main processes"},
  {0,0}
};

static char* argv0;

httpd_conn* allConnects[FD_SETSIZE/2];
int maxConnects;

int fdToConnMap[FD_SETSIZE];

httpd_server HS;

/* ---------------------------------------------------------------- */
static void
InitConnectStates(int maxConns, int acceptFD)
{
  int i;

  for (i=0; i < FD_SETSIZE/8; i++)
    freeConnBits[i] = 0xff;
  firstFreeConnHint = 0;

  for (i = 0; i < FD_SETSIZE; i++)
    fdToConnMap[i] = -2;
}
/* ---------------------------------------------------------------- */
static void
ShutDown(void)
{
  int cnum;
  
  for (cnum = 0; cnum < maxConnects; ++cnum)
    if (!ISCONNFREE(cnum))
      DoneWithConnection(allConnects[cnum], TRUE);
  KillAllFreeCGISocks();
  HttpdTerminate();
}
/* ---------------------------------------------------------------- */
static void
HandleKill(int sig)
{
  ShutDown();
  fprintf(stderr, "exiting due to signal %d\n", sig);
  exit(1);
}
/* ---------------------------------------------------------------- */
int
main(int argc, char** argv)
{
  char* cp;
  int debug = 1;		/* keep debugging on always */
  int port;
  int do_chroot;
  struct passwd* pwd;
  uid_t uid;
  gid_t gid;
  char cwd[MAXPATHLEN];
  int nfiles;
  int i;

  systemPageSizeBits = systemPageSize = getpagesize();
  for (i = 0; ; i++) {
    if (systemPageSizeBits <= 1) {
      systemPageSizeBits = i;
      break;
    }
    systemPageSizeBits >>= 1;
  }
  
  ScanOptions(argc, argv, 1, stringOptions);
  port = atoi(bindPortStr);
  do_chroot = atoi(doChrootStr);
  maxLRUCacheSizeBytes = 1024*1024*atoi(lruCacheMBStr);
  maxReadHelp = atoi(maxReadHelpStr);
  maxConvHelp = atoi(maxConvHelpStr);
  maxDirHelp = atoi(maxDirHelpStr);
  initReadHelp = atoi(initReadHelpStr);
  initConvHelp = atoi(initConvHelpStr);
  sendBufSizeBytes = 1024 * atoi(sendBufKBStr);
  slaveDelayTime = atoi(slaveDelayTimeStr);
  doQueueLenDumps = atoi(doQueueLenDumpsStr);
  doMainStats = atoi(doMainStatsStr);
  doMincoreDump = atoi(doMincoreDumpStr);
  maxNameCacheSize = atoi(nameCacheSizeStr);
  maxSlaveIdleTime = atoi(maxSlaveIdleTimeStr);
  if (maxSlaveIdleTime < 0)
    maxSlaveIdleTime = 0;
  numFlashProcs = atoi(numFlashProcsStr);
  if (numFlashProcs < 1)
    numFlashProcs = 1;

  /* sets up logging if needed */
  SetUpAccessLogging(accessLogName);

  if (maxLRUCacheSizeBytes < 0)
    Panic("cannot have negative lru size");
  
  if (maxReadHelp < 1 || maxConvHelp < 1 || maxDirHelp < 1)
    Panic("must allow at least one of each helper");

  if (initReadHelp < 0 || initConvHelp < 0 ||
      initReadHelp > maxReadHelp || initConvHelp > maxConvHelp)
    Panic("initial number of helpers out of range");
  
  if (sendBufSizeBytes < 0 || sendBufSizeBytes > 256*1024)
    Panic("send buffer size out of range");

  if (slaveDelayTime < 0)
    Panic("cannot have negative slave delay time");

  if (maxNameCacheSize < 1)
    Panic("must have some name cache size");

  argv0 = argv[0];
  
  cp = strrchr(argv0, '/');
  if (cp != (char*) 0)
    ++cp;
  else
    cp = argv0;
  
  signal(SIGINT, HandleKill);
  signal(SIGTERM, HandleKill);
  signal(SIGPIPE, SIG_IGN);		/* get EPIPE instead */
  
  InitFDSets();

  /* Check port number. */
  if (port <= 0) {
    fprintf(stderr, "illegal port number\n");
    exit(1);
  }
  
  /* Figure out uid/gid from user. */
  pwd = getpwnam(serverUserStr);
  if (pwd == (struct passwd*) 0) {
    fprintf(stderr, "unknown user - '%s'\n", serverUserStr);
    exit(1);
  }
  uid = pwd->pw_uid;
  gid = pwd->pw_gid;
  
  /* figure out absolute pathname for this program */
  if (argv[0][0] == '/')
    strcpy(cwd, argv[0]);
  else {
    if (!getcwd(cwd, sizeof(cwd))) {
      perror("getcwd");
      exit(-1);
    }
    if (cwd[strlen(cwd) - 1] != '/')
      strcat(cwd, "/");
    strcat(cwd, argv[0]);
  }

  /* strip program name - inefficient but who cares */
  while (strlen(cwd) && cwd[strlen(cwd) - 1] != '/')
    cwd[strlen(cwd) - 1] = 0;

  /* Initialize the HTTP layer.  Got to do this before giving up root,
   ** so that we can bind to a privileged port.
   */
  if (HttpdInitialize(hostnameStr, port, cgiPatternStr, cwd))
    exit(1);
  
  /* Switch directories if requested. */
  if (docDirStr != (char*) 0) {
    if (chdir(docDirStr) < 0) {
      perror("chdir");
      exit(1);
    }
  }

#ifdef USE_USER_DIR
  else if (getuid() == 0) {
    /* No explicit directory was specified, we're root, and the
     ** USE_USER_DIR option is set - switch to the specified user's
     ** home dir.
     */
    if (chdir(pwd->pw_dir) < 0) {
      perror("chdir");
      exit(1);
    }
  }
#endif /* USE_USER_DIR */
  
  for (i = 1; i < numFlashProcs; i++) {
    int forkRes;
    forkRes = fork();
    if (forkRes == 0)		/* child proccess */
      break;
    if (forkRes < 0) {		/* failure */
      fprintf(stderr, "failed forking main procs - only %d exists\n", i);
      break;
    }
  }
  
  /* start the slaves _after_ changing the application directory */
  InitConvertSlaves(cwd);
  InitAsyncReadSlaves(cwd);
  InitDirSlaves(cwd);

  /* Get current directory. */
  getcwd(cwd, sizeof(cwd));
  if (cwd[strlen(cwd) - 1] != '/')
    strcat(cwd, "/");
  
  /* Chroot if requested. */
  if (do_chroot) {
    if (chroot(cwd) < 0) {
      perror("chroot");
      exit(1);
    }
    strcpy(cwd, "/");
  }
  
  if (! debug) {
    /* We're not going to use stdin stdout or stderr from here on, so close
     ** them to save file descriptors.
     */
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    /* Daemonize - make ourselves a subprocess. */
    switch (fork()) {
    case 0:
      break;
    case -1:
      perror("fork");
      exit(1);
    default:
      exit(0);
    }
  }
  else {
    /* for some reason, we're getting a weird bug if we
       leave either of these open. Stderr seems unaffected */
    fclose(stdin);
    fclose(stdout);
  }
  
  /* If we're root, try to become someone else. */
  if (getuid() == 0) {
    if (setgid(gid) < 0) {
      perror("setgid");
      exit(1);
    }
    if (setuid(uid) < 0) {
      perror("setuid");
      exit(1);
    }
    /* Check for unnecessary security exposure. */
    if (!do_chroot)
      fprintf(stderr, 
	      "started as root without requesting chroot(), warning only\n");
  }

  /* Figure out how many file descriptors we can use. */
  nfiles = MIN(HttpdGetNFiles(), FD_SETSIZE);
  
  /* conservative estimate:
     each connection can require 2 fds - one to the client, and
     the other either to the filesystem or the cgi app */
  maxConnects = (nfiles - (SPARE_FDS + maxReadHelp + maxConvHelp))/2;
  fprintf(stderr, "%d max connections allowed\n", maxConnects);
  
  if (maxConnects < 1)
    Panic("too many helpers specified");

  if (maxConnects < 64)
    fprintf(stderr, "WARNING: large # of helpers limits # connections\n");

  gettimeofday(&globalTimeOfDay, (struct timezone*) 0);

  InitConnectStates(maxConnects, HS.fd);

  lastTimerCheckTime = globalTimeOfDay.tv_sec;

  MainLoop();

  return(0);			/* will never reach here */
}
/* ---------------------------------------------------------------- */
