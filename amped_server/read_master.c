#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include "common.h"

int numActiveReadSlaves;

typedef struct HelperInfo {
  int hi_fd;
  httpd_conn *hi_hc;
} HelperInfo;

static HelperInfo *helperInfo;	

static httpd_conn *waitingList;

static int CurrentReaderDone(httpd_conn *hc, int fd, int forRW);

static char readSlaveName[MAXPATHLEN];
static int HelperGoneIdle(httpd_conn *hc, int fd, int forRW);

static struct timeval timeWaitListStarted;

/* ---------------------------------------------------------------- */
int
CreateGenericSlave(char *slaveName)
{
  /* forks off a slave and executes the specific program.
     The new directory (if set) is passed to the slave as the only
     argument. stderr is kept open in the slave, as is a 2-way
     communication socket with the parent. This socket number
     is returned on success, or -1 on (detectable) failure */

  int r;
  int sockVec[2];
  char *newDir;

  newDir = NULL;		/* we're doing this after the chdir */

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockVec) < 0) {
    fprintf(stderr, "error doing sockpair for %s\n", slaveName);
    return(-1);
  }
  
  r = fork();
  if (r < 0) {
    fprintf(stderr, "failed forking for %s\n", slaveName);
    return(-1);
  }

  if (r == 0) {
    /* we're in child */
    int j;
    static char timeoutStr[20];
    char *args[4];

    args[0] = slaveName;
    args[1] = timeoutStr;
    args[2] = newDir;		/* newDir might be NULL, but that's OK */
    args[3] = NULL;  
    
    sprintf(timeoutStr, "%d", maxSlaveIdleTime);
    close(sockVec[0]);	/* close parent's end */
    
    /* make communication socket fd#0 for child */
    if (dup2(sockVec[1], 0) < 0) {
      fprintf(stderr, "error while duping child for %s\n", slaveName);
      exit(-1);
    }
    
    /* close all other fd's in child */
    for (j = HttpdGetNFiles() - 1; j > 0; j--) {
      if (j != STDERR_FILENO)	/* keep stderr open */
	close(j);
    }
    
    if (execv(slaveName, args) < 0) {
      fprintf(stderr, "error while execing %s\n", slaveName);
      exit(-1);
    }
    /* child never reaches here */
  }

  /* we're in parent */
  close(sockVec[1]);	/* close child's end */
  return(sockVec[0]);
}
/* ---------------------------------------------------------------- */
void
InitAsyncReadSlaves(char *cwd)
{
  char *progName;

  helperInfo = calloc(maxReadHelp, sizeof(HelperInfo));
  if (!helperInfo)
    Panic("out of memory initing read slaves");

  progName = "read_slave_mmap";

  sprintf(readSlaveName, "%s%s", cwd, progName);

  if (initReadHelp) {
    int i;
    assert(initReadHelp <= maxReadHelp);
    for (i = 0; i < initReadHelp; i++) {
      int fd;
      fd = helperInfo[i].hi_fd = CreateGenericSlave(readSlaveName);
      if (helperInfo[i].hi_fd >= 0) {
	numActiveReadSlaves++;
	ClearConnFDAssociation(fd);
	SetSelectHandler(fd, HelperGoneIdle, SSH_READ);
      }
      else {
	fprintf(stderr, "died in read slave\n");
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
  char temp[1024];
  int len;

  sprintf(temp, "%d %d %s", hc->hc_asyncByteRead, hc->hc_asyncReadLen,
	  hc->hc_cacheEnt->ce_filename);
  len = 1 + strlen(temp);

  res = write(helperInfo[pos].hi_fd, temp, len);
  if (res !=  len) {
    fprintf(stderr, "helperInfo[pos].hi_fd %d\n", helperInfo[pos].hi_fd);
    fprintf(stderr, "couldn't write async read info, res %d\n", res);
    perror("async read");
    exit(-1);
  }
  helperInfo[pos].hi_hc = hc;
  MakeConnFDAssociation(hc, helperInfo[pos].hi_fd);
  SetSelectHandler(helperInfo[pos].hi_fd, CurrentReaderDone, SSH_READ);

  mainStats.cs_totRealReadBytes += hc->hc_asyncReadLen;
  mainStats.cs_totRealReadPages += 
    (hc->hc_asyncReadLen + systemPageSize-1) >> systemPageSizeBits;
}
/* ---------------------------------------------------------------- */
static int
MatchHelperToPos(int fd, httpd_conn *hc)
{
  int i;

  /* find which entry */
  for (i = 0; i < numActiveReadSlaves; i++) {
    if (helperInfo[i].hi_fd == fd)
      break;
  }
  if (i >= numActiveReadSlaves){
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
HelperGoneIdle(httpd_conn *hc, int fd, int forRW)
{
  int i;
  char line[256];

  i = MatchHelperToPos(fd, NULL);

  if (read(fd, line, 5) != 5 ||
      strcmp(line, "idle")) {
    fprintf(stderr, "message: %s\n", line);
    Panic("helper returned short message");
  }

  /* if we've assigned a connection, don't close */
  if (helperInfo[i].hi_hc)
    return(0);

  close(helperInfo[i].hi_fd);
  ClearConnFDAssociation(fd);
  SetSelectHandler(fd, NULL, SSH_NOTHING);

  /* shift what's left */
  numActiveReadSlaves--;
  for (; i < numActiveReadSlaves; i++) 
    helperInfo[i] = helperInfo[i+1];
  return(0);
}
/* ---------------------------------------------------------------- */
static int
CurrentReaderDone(httpd_conn *hc, int fd, int forRW)
{
  char junk[100];
  int bytes;
  int i;

  ClearConnFDAssociation(fd);
  SetSelectHandler(fd, HelperGoneIdle, SSH_READ);

  i = MatchHelperToPos(fd, hc);
  helperInfo[i].hi_hc = NULL;	/* mark empty */

  bytes = read(fd, junk, sizeof(junk));
  if (bytes < 1) {
    fprintf(stderr, "read_slave had error?\n");
    exit(-1);
  }

  if (!strcmp(junk, "idle")) {
    if (bytes == 5) {
      /* crossed wires - ignore */
      AddCurrentReader(hc, i);
      return(0);
    }
    /* clip this part of message, continue */
    for (i = 5; i < bytes; i++)
      junk[i-5] = junk[i];
  }

  mainStats.cs_numActualReads++;

  if (strcmp(junk, "done"))
    fprintf(stderr, "read_slave: %s\n", junk);

  /* activate entire sibling list */
  while (hc) {
    httpd_conn *temp;

    /* Separate each one before
       resuming - prevents looping */
    temp = hc;
    hc = hc->hc_siblings;
    ResumeAfterFileReading(temp);
  }

  /* if waiting list, schedule new reader */
  if (!waitingList)
    return(0);
  hc = waitingList;
  waitingList = hc->hc_next;
  hc->hc_next = NULL;
  AddCurrentReader(hc, i);

  if (!waitingList)
    timeWaitListStarted.tv_sec = 0;

  return(0);
}
/* ---------------------------------------------------------------- */
void
ScheduleAsyncRead(struct httpd_conn *hc, int startByte, int length)
{
  int i;
  httpd_conn *walk;
  httpd_conn *tail = NULL;
  int newSlave;

  mainStats.cs_numReadBlocks++;

  SetSelectHandler(hc->hc_fd, NULL, SSH_NOTHING);

  /* although we store start and length, we can assume
     that if it has the same start, it has the same length */

  hc->hc_asyncReadLen = length;
  hc->hc_asyncByteRead = startByte;
  hc->hc_next = NULL;
  hc->hc_siblings = NULL;

  /* we can compare the actual cache entry, since all connections
     for the same file should share the same entry */

  /* try to join with current reader */
  for (i = 0; i < numActiveReadSlaves; i++) {
    if (helperInfo[i].hi_hc && 
	helperInfo[i].hi_hc->hc_asyncByteRead == startByte &&
	helperInfo[i].hi_hc->hc_cacheEnt == hc->hc_cacheEnt) {
      hc->hc_siblings = helperInfo[i].hi_hc->hc_siblings;
      helperInfo[i].hi_hc->hc_siblings = hc;
      return;
    }
  }

  /* get free spot if one's open */
  for (i = 0; i < numActiveReadSlaves; i++) {
    if (!helperInfo[i].hi_hc) {
      AddCurrentReader(hc, i);
      return;
    }
  }

  /* try to join with waiting list */
  for (walk = waitingList; walk; walk = walk->hc_next) {
    if (walk->hc_asyncByteRead == startByte &&
	walk->hc_cacheEnt == hc->hc_cacheEnt) {
      hc->hc_siblings = walk->hc_siblings;
      walk->hc_siblings = hc;
      return;
    }
    tail = walk;
  }

  /* create a new slave if we've got free spots 
     and if the timing works */

  newSlave = FALSE;
  if (!numActiveReadSlaves) 
    newSlave = TRUE;
  else if (numActiveReadSlaves >= maxReadHelp)
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

    i = numActiveReadSlaves;
    helperInfo[i].hi_fd = CreateGenericSlave(readSlaveName);
    if (helperInfo[i].hi_fd >= 0) {
      numActiveReadSlaves++;
      AddCurrentReader(hc, i);
      return;
    }
    else {
      fprintf(stderr, "failed in lazy fork for %s\n", readSlaveName);
      if (!numActiveReadSlaves)
	exit(-1);
    }
  }

  /* go to end of list */
  if (tail)
    tail->hc_next = hc;
  else {
    waitingList = hc;
    timeWaitListStarted = globalTimeOfDay;
  }
}
/* ---------------------------------------------------------------- */
