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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <dirent.h>
#include <assert.h>
#include "config.h"
#include "handy.h"

#define OUTFD 0

static int currResponseUsed;
static char *currResponse;
static int currResponseAllocSize;

typedef struct FileEntry {
  struct FileEntry *fe_next;
  char *fe_name;
  int fe_sizeInK;
  time_t fe_modTime;
} FileEntry;

/* ---------------------------------------------------------------- */
static FileEntry *
GenerateRawFeList(char *dirName, DIR *dirp, int *numEnts)
{
  FileEntry *feList = NULL;
  int numFeItems = 0;
  struct dirent *de;
  char workPath[MAXPATHLEN];
  struct stat buf;
  static FileEntry *newItem;

  while ((de = readdir(dirp)) != 0) {	/* dirent or direct */
    if (!newItem)
      newItem = calloc(1, sizeof(FileEntry));
    if (!newItem) {
      fprintf(stderr, "failed calloc in GenerateRawFeList\n");
      *numEnts = numFeItems;
      return(feList);
    }

    /* skip any hidden files or empty names */
    if (de->d_name[0] == '.' || de->d_name[0] == '\0')
      continue;

    newItem->fe_name = calloc(1, strlen(de->d_name) + 3);
    if (!newItem->fe_name) {
      *numEnts = numFeItems;
      return(feList);
    }
    strcpy(newItem->fe_name, de->d_name);

    sprintf(workPath, "%s/%s", dirName, newItem->fe_name);
    if (stat(workPath, &buf) < 0) { /* ignore any bad files */
      fprintf(stderr, "tossing %s\n", workPath);
      continue;
    }

    /* not world-readable, so don't include in list */
    if ((buf.st_mode & S_IROTH) == 0) 
      continue;

    if (buf.st_mode & S_IFDIR)	/* add slash for directories */
      strcat(newItem->fe_name, "/"); 

    newItem->fe_sizeInK = (1023 + buf.st_size)/1024;
    newItem->fe_modTime = buf.st_mtime;
    newItem->fe_next = feList;
    feList = newItem;
    numFeItems++;
    newItem = NULL;		/* force next to be alloc'd */
  }

  *numEnts = numFeItems;
  return(feList);
}
/* ---------------------------------------------------------------- */
int
FeCompFunc(const void *aP, const void *bP)
{
  FileEntry *a, *b;

  a = *((FileEntry **)aP);
  b = *((FileEntry **)bP);

  return(strcmp(a->fe_name, b->fe_name));
}
/* ---------------------------------------------------------------- */
static FileEntry *
SortFeList(FileEntry *feList, int numFeItems)
{
  FileEntry **fePtrs;
  int i;
  FileEntry *walk;

  assert(numFeItems > 0);
  fePtrs = calloc(numFeItems, sizeof(FileEntry *));
  if (!fePtrs) {
    fprintf(stderr, "failed calloc in SortFeList\n");
    return(NULL);
  }

  for (i = 0, walk = feList; i < numFeItems; i++, walk = walk->fe_next) {
    if (!walk) {
      fprintf(stderr, "mismatch in SortFeList\n");
      return(NULL);		
    }
    fePtrs[i] = walk;
  }

  qsort(fePtrs, numFeItems, sizeof(FileEntry *), FeCompFunc);

  for (i = 0; i < numFeItems-1; i++)
    fePtrs[i]->fe_next = fePtrs[i+1];
  fePtrs[numFeItems-1]->fe_next = NULL;
  feList = fePtrs[0];
  free(fePtrs);
  return(feList);
}
/* ---------------------------------------------------------------- */
static int
AddStringToResponse(char *str)
{
  int len;

  if (currResponse == NULL) {
    currResponseAllocSize = 3 * MAXPATHLEN;
    currResponse = calloc(1, currResponseAllocSize);
    currResponseUsed = 0;
    if (!currResponse)
      return(TRUE);
  }

  len = strlen(str);
  if (len + 1 + currResponseUsed > currResponseAllocSize) {
    char *newBuf;
    currResponseAllocSize += len + MAXPATHLEN;
    newBuf = realloc(currResponse, currResponseAllocSize);
    if (!newBuf)
      return(TRUE);
    currResponse = newBuf;
  }
  memcpy(currResponse+currResponseUsed, str, len+1);
  currResponseUsed += len;
  return(FALSE);
}
/* ---------------------------------------------------------------- */
static void
EmptyResponse(void)
{
  currResponseUsed = 0;
  if (currResponseAllocSize > 3 * MAXPATHLEN) {
    free(currResponse);
    currResponse = NULL;
    currResponseAllocSize = 0;
  }
}
/* ---------------------------------------------------------------- */
static char *
MakeListDate(time_t unixTime)
{
  /* date generated by this function will be constant-length.
     if it's not, very bad things can happen */

  static char dateSpace[50];
  static char *monthNames[] = {
    "Jan", "Feb", "Mar", "Apr", 
    "May", "Jun", "Jul", "Aug", 
    "Sep", "Oct", "Nov", "Dec"};
  static char *dayNames[] = {
    "Sun", "Mon", "Tue", 
    "Wed", "Thu", "Fri", "Sat"};
  struct tm *lct;
  
  lct = localtime(&unixTime);

  sprintf(dateSpace, "%s, %2d %s %d %2d:%.2d", 
	  dayNames[lct->tm_wday], 
	  lct->tm_mday, monthNames[lct->tm_mon], lct->tm_year + 1900,
	  lct->tm_hour, lct->tm_min);
  return(dateSpace);
}
/* ---------------------------------------------------------------- */
static void
CreateListing(char *realDir, char *userName, DIR *dirp)
{
  FileEntry *feList;
  int numFeItems = 0;
  char workSpace[3*MAXPATHLEN];

  feList = GenerateRawFeList(realDir, dirp, &numFeItems);
  if (feList) {
    FileEntry *newList;
    newList = SortFeList(feList, numFeItems);
    if (newList)
      feList = newList;
  }

  sprintf(workSpace, 
	  "<HTML><HEAD><TITLE>Index of %.80s</TITLE></HEAD>\n"
	  "<BODY>\n"
	  "<H2>Index of %.80s</H2>\n"
	  "<PRE>\n"
	  " %-20s %-22s %5s\n"
	  "<HR>",
	  userName, userName, "name", "last modified time", "size");
  AddStringToResponse(workSpace);

  while (feList) {
    FileEntry *tempList;
    char pad[21];

    memset(pad, ' ', sizeof(pad));
    pad[sizeof(pad)-1] = '\0';
    if (strlen(feList->fe_name) > strlen(pad))
      pad[0] = '\0';
    else
      pad[strlen(pad) - strlen(feList->fe_name)] = '\0';

    sprintf(workSpace, " <A HREF=\"%s\">%s</A>%s %22s %4dk\n", 
	    feList->fe_name, feList->fe_name, pad,
	    MakeListDate(feList->fe_modTime), feList->fe_sizeInK);
    AddStringToResponse(workSpace);
    tempList = feList->fe_next;
    free(feList->fe_name);
    free(feList);
    feList = tempList;
  }

  AddStringToResponse("</PRE></BODY></HTML>\n");
}
/* ---------------------------------------------------------------- */
void main(int argc, char *argv[])
{
  char bothDirNames[2*MAXPATHLEN+3];
  char *realDirName, *userVisibleName;
  int sendReply = FALSE;
  fd_set rfdset;
  struct timeval selectTimeout;
  int replyVal = 0;
  DIR *dirp;
  int doHead = FALSE;

  if ((argc > 2) && (chdir(argv[2]) < 0)) {
    fprintf(stderr, "failed to switch to slave dir\n");
    exit(-1);
  }

  FD_ZERO(&rfdset);
  selectTimeout.tv_sec = atoi(argv[1]);
  selectTimeout.tv_usec = 0;

  while (1) {
    int ret;
    int bytesRead;
    if (sendReply) {
      write(OUTFD, &replyVal, sizeof(replyVal));

      if (replyVal > 0 && (!doHead)) {
	int bytesWrit;
	bytesWrit = write(OUTFD, currResponse, replyVal);
	if (bytesWrit != replyVal)
	  fprintf(stderr, "ls didn't write full reply - %d out of %d\n",
		  bytesWrit, replyVal);
      }

      EmptyResponse();
    }
    sendReply = TRUE;
    replyVal = 0;

    if (selectTimeout.tv_sec) {
      FD_SET(0, &rfdset);
      if (!select(1, &rfdset, NULL, NULL, &selectTimeout)) {
	/* time limit expired - tell master we're idle */
	replyVal = -1;
	ret = write(OUTFD, &replyVal, sizeof(replyVal));
	if (ret != sizeof(replyVal)) {
	  fprintf(stderr, "ls slave couldn't write idle - %d\n", ret);
	  perror("write");
	  exit(0);
	}
	sendReply = FALSE;
	continue;
      }
    }

    bytesRead = 0;
    do {
      ret = read(0, &bothDirNames[bytesRead], 
		 sizeof(bothDirNames) - bytesRead);
      if (ret == 0)		/* master closed connection */
	exit(0);
      if (ret < 0) {
	fprintf(stderr, "ls slave had negative\n");
	exit(0);
      }
      bytesRead += ret;
    } while (bothDirNames[bytesRead-1] != '\0');

    doHead = (bothDirNames[0] == 'h') ? TRUE : FALSE;
    realDirName = userVisibleName = &bothDirNames[1];
    while (*userVisibleName != ' ' &&
	   *userVisibleName != '\0')
      userVisibleName++;

    if (*userVisibleName == ' ') {
      *userVisibleName = '\0';
      userVisibleName++;
    }
    else {
      fprintf(stderr, "couldn't find user visible name\n");
      userVisibleName = realDirName;
    }

    dirp = opendir(realDirName);
    if (!dirp) 
      continue;
    if (doHead) {
      closedir(dirp);
      replyVal = 1;		/* any positive value */
      continue;
    }

    CreateListing(realDirName, userVisibleName, dirp);
    closedir(dirp);
    replyVal = currResponseUsed+1;
  }
}
/* ---------------------------------------------------------------- */
