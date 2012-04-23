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
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>

#include "datacache.h"
#include "version.h"
#include "libhttpd.h"		/* just for globaltimeofday */
#include "handy.h"
#include "config.h"
#include "common.h"

typedef struct ReadInfoRec {
  int rir_numRefs;
  void *rir_data;

  /* below here is experimental stuff for mmap */
  struct ReadInfoRec *rir_prev;
  struct ReadInfoRec *rir_next;
  struct CacheEntry *rir_entry;
  int rir_chunkNum;
  int rir_isOnMRU;
} ReadInfoRec;

typedef struct DelayedUnmap {
  void *du_addr;
  int du_len;
  struct DelayedUnmap *du_next;
} DelayedUnmap;

/* mru only contains chunks without references */
static ReadInfoRec *freeChunkMRUHead;
static ReadInfoRec *freeChunkMRUTail;

static long bytesInAllChunks;	/* rounded size of all chunks */
static long bytesInFreeChunks; /* rounded size of all chunks on free list */

#define FILECACHEVALIDATETIME 300

static void KillEntry(CacheEntry *ent);
static DelayedUnmap *TryFreeChunk(CacheEntry *ent, int chunkNum, int doDelay);
static void RemoveFromChunkLRUList(struct CacheEntry *ent, int chunk);

/* ------------------------------------------------------------------*/
#define NUMNAMEHASHBINS 1024	/* update hash function if this changes */
#define NUMNAMEHASHBINBITS 10	/* log2(NUMNAMEHASHBINS) */
static CacheEntry *cacheBins[NUMNAMEHASHBINS];
static CacheEntry *invalidEntriesHead;

/* ------------------------------------------------------------------*/
static int
GetRoundedChunkSize(CacheEntry *ce, int chunk)
{
  int size;

  if (chunk < ce->ce_numChunks - 1)
    return(READBLOCKSIZE);
  if (chunk == 0)
    size = ce->ce_size;
  else
    size = ce->ce_size % READBLOCKSIZE;
  size += systemPageSize -1;
  size -= size & (systemPageSize -1);
  return(size);
}
/* ------------------------------------------------------------------*/
void
DumpDataCacheStats(void)
{
  int i;
  int lenCounts[21];
  int totalEntries = 0;
  double avgLen = 0;

  for (i = 0; i < 21; i++)
    lenCounts[i] = 0;

  for (i = 0; i < NUMNAMEHASHBINS; i++) {
    int len = 0;
    CacheEntry *walk;
    for (walk = cacheBins[i]; walk; walk = walk->ce_nextInHash) 
      len++;
    totalEntries += len;
    avgLen += .5*len*(len+1);
    if (len > 20)
      len = 20;
    lenCounts[len]++;
  }

  fprintf(stderr, "data cache lengths (truncated at 20): %d entries total\n", 
	  totalEntries);
  if (totalEntries)
    avgLen /= totalEntries;	/* static avg seen by entries */
  for (i = 0; i < 21; i++)
    if (lenCounts[i])
      fprintf(stderr, "length %d: %d\n", i, lenCounts[i]);
  fprintf(stderr, "avg len %.2f\n", avgLen);

}
/* ------------------------------------------------------------------*/
static DelayedUnmap *
ReduceCacheIfNeeded(void)
{
  DelayedUnmap *unmapList = NULL;

  if (!maxLRUCacheSizeBytes)
    return(NULL);

  while (bytesInAllChunks > maxLRUCacheSizeBytes &&
	 bytesInFreeChunks > 0) {
    CacheEntry *victEnt;
    ReadInfoRec *rir;

    rir = freeChunkMRUTail;
    if (!rir) {
      fprintf(stderr, "no rir in chunk overflow\n");
      exit(-1);
    }
    victEnt = rir->rir_entry;
    RemoveFromChunkLRUList(victEnt, rir->rir_chunkNum);
    TryFreeChunk(victEnt, rir->rir_chunkNum, FALSE);
    if (victEnt->ce_numChunksInMem == 0) {
      if (victEnt->ce_numRefs) {
	if (victEnt->ce_size <= READBLOCKSIZE)
	  fprintf(stderr, "had entry with refs but no chunks - size %d\n", 
		  victEnt->ce_size);
      }
      else {
	KillEntry(victEnt);
	victEnt = NULL;		/* safety */
      }
    }
  }
  return(unmapList);
}
/* ------------------------------------------------------------------*/
static void
RemoveFromChunkLRUList(struct CacheEntry *ent, int chunk)
{
  ReadInfoRec *rir;

  rir = &ent->ce_dataChunks[chunk];

  if (!rir->rir_isOnMRU)
    return;

  if (rir->rir_prev)
    rir->rir_prev->rir_next = rir->rir_next;
  else
    freeChunkMRUHead = rir->rir_next;
  if (rir->rir_next)
    rir->rir_next->rir_prev = rir->rir_prev;
  else
    freeChunkMRUTail = rir->rir_prev;

  bytesInFreeChunks -= GetRoundedChunkSize(ent, chunk);
  rir->rir_isOnMRU = FALSE;
}
/* ------------------------------------------------------------------*/
static DelayedUnmap *
AddToChunkLRUList(struct CacheEntry *ent, int chunk)
{
  ReadInfoRec *rir;

  rir = &ent->ce_dataChunks[chunk];

  if (rir->rir_isOnMRU)
    return(NULL);

  rir->rir_chunkNum = chunk;
  rir->rir_entry = ent;

  rir->rir_isOnMRU = TRUE;
  rir->rir_prev = NULL;
  rir->rir_next = freeChunkMRUHead;
  if (freeChunkMRUHead)
    freeChunkMRUHead->rir_prev = rir;
  else
    freeChunkMRUTail = rir;
  freeChunkMRUHead = rir;

  bytesInFreeChunks += GetRoundedChunkSize(ent, chunk);

  return(ReduceCacheIfNeeded());
}
/* ------------------------------------------------------------------*/
static int 
HashFilename(unsigned char *name)
{
  int val;

  val = GenericStringHash(name);
  val += (val >> NUMNAMEHASHBINBITS) + (val >> (2*NUMNAMEHASHBINBITS));
  return(val & (NUMNAMEHASHBINS-1));
}
/* ------------------------------------------------------------------*/
static int 
DoChunkRead(CacheEntry *ent, int chunk, int *shadowFD, void **shadowMap)
{
  int chunkStart, chunkEnd;
  int chunkSize;
  void *res;

  chunkStart = chunk * READBLOCKSIZE;
  chunkEnd = MIN(chunkStart + READBLOCKSIZE, ent->ce_size);
  chunkSize = chunkEnd - chunkStart;

  if (ent->ce_file_fd < 0) {
    /* file was closed, so re-open it */
    if (shadowFD && *shadowFD >= 0) {
      /* since shadow fd was passed in, use it and steal it */
      ent->ce_file_fd = *shadowFD;
      *shadowFD = -1;
    }
    else
      ent->ce_file_fd = open(ent->ce_filename, O_RDONLY);
    if (ent->ce_file_fd < 0)
      return(TRUE);
  }

  if (shadowMap && *shadowMap) {
    /* since shadow mapping was passed in, use it and steal it */
    res = *shadowMap;
    *shadowMap = NULL;
  }
  else
    res = mmap(0, chunkSize, PROT_READ, MAP_SHARED,
	       ent->ce_file_fd, chunkStart);
  if (res == (char *) -1) {
    fprintf(stderr, "mmap - %d\n", errno);
    return(TRUE);
  }

  ent->ce_dataChunks[chunk].rir_data = res;
  ent->ce_numChunksInMem++;
  bytesInAllChunks += GetRoundedChunkSize(ent, chunk);
  ReduceCacheIfNeeded();
  return(FALSE);
}
/* ------------------------------------------------------------------*/
int
IncChunkLock(struct CacheEntry *ent, int bytes) 
{
  int chunk = bytes / READBLOCKSIZE;
  ReadInfoRec *rir;

  if (ent == NULL || ent->ce_dataChunks == NULL) {
    fprintf(stderr, "trying to inc null chunks\n");
    exit(-1);
  }

  if (chunk >= ent->ce_numChunks) 
    return(0);

  rir = &ent->ce_dataChunks[chunk];
  if (rir->rir_numRefs == 0) 
    RemoveFromChunkLRUList(ent, chunk);
  rir->rir_numRefs++;

  return(1);
}
/* ------------------------------------------------------------------*/
int
DecChunkLock(struct CacheEntry *ent, int bytes) 
{
  int chunk = bytes / READBLOCKSIZE;
  int deadFileFd = -1;
  DelayedUnmap *duList = NULL;

  if (ent == NULL || ent->ce_dataChunks == NULL) {
    fprintf(stderr, "trying to dec null chunks\n");
    exit(-1);
  }
  if (chunk >= ent->ce_numChunks) 
    return(0);

  ent->ce_dataChunks[chunk].rir_numRefs--;

  if (ent->ce_dataChunks[chunk].rir_numRefs < 1) {
    if (ent->ce_dataChunks[chunk].rir_numRefs < 0) {
      fprintf(stderr, "got neg chunk lock count\n");
      exit(-1);
    }

    duList = AddToChunkLRUList(ent, chunk);
  }

  /* if we're doing an unlock on the final chunk,
     we can close the file assuming it won't be needed soon */
  if (chunk == ent->ce_numChunks - 1 &&
      ent->ce_file_fd != -1) {
    /* we can close the file in question */
    deadFileFd = ent->ce_file_fd;
    ent->ce_file_fd = -1;
  }

  if (deadFileFd >= 0)
    if (close(deadFileFd))
      perror("close");

  return(1);
}
/* ------------------------------------------------------------------*/
int
ReadChunkIfNeeded(struct CacheEntry *ent, int position)
{
  int chunkNum;
  int tempfd = -1;
  void *tempMap = NULL;
  int chunkSize = 0;
  int chunkReadVal = 0;

  if (!ent->ce_size)		/* nothing needs to be read */
    return(FALSE);

  chunkNum = position / READBLOCKSIZE;

  if (ent->ce_dataChunks[chunkNum].rir_data) {
    return(FALSE);
  }

  /* check again before doing read, to handle case of
     someone else doing read while we lost mutex in MT case */
  if (!ent->ce_dataChunks[chunkNum].rir_data) 
    chunkReadVal = DoChunkRead(ent, chunkNum, &tempfd, &tempMap);

  if (tempfd >= 0)
    close(tempfd);
  if (tempMap)
    munmap(tempMap, chunkSize);
  if (chunkReadVal)
    return(TRUE);
  return(FALSE);
}
/* ------------------------------------------------------------------*/
void *
GetDataToSend(struct CacheEntry *ent, int position, int desiredSize, 
	      int maxSize, int *actualSize, int *startOffset)
{
  int chunkNum;
  int bytesLeft;
  int posMod;

  if (ent->ce_size == 0) {
    *actualSize = *startOffset = 0;
    return(NULL);
  }

  chunkNum = position / READBLOCKSIZE;
  posMod = position % READBLOCKSIZE;

  if (chunkNum == ent->ce_numChunks-1)
    bytesLeft = ent->ce_size - position;
  else
    bytesLeft = READBLOCKSIZE - posMod;

  /* if we don't have the data, quit early */
  if (!ent->ce_dataChunks[chunkNum].rir_data) {
    *actualSize = bytesLeft;
    return(NULL);
  }

  if (bytesLeft > maxSize) {
    if (bytesLeft >= desiredSize*2)
      bytesLeft = desiredSize;
    else
      while (bytesLeft > desiredSize)
	bytesLeft >>=1;
  }

  *actualSize = bytesLeft;
  *startOffset = posMod;
  return(ent->ce_dataChunks[chunkNum].rir_data);
}
/* ------------------------------------------------------------------*/
int 
AddEntryToDataCache(CacheEntry *ce)
{
  int hashBin;
  int numChunks;

  ce->ce_numRefs = 1;

  numChunks = (ce->ce_size + READBLOCKSIZE-1) / READBLOCKSIZE;
  if (!numChunks)
    numChunks = 1;

  ce->ce_numChunks = numChunks;
  ce->ce_numChunksInMem = 0;
  if (ce->ce_dataChunks)
    free(ce->ce_dataChunks);
  ce->ce_dataChunks = calloc(numChunks, sizeof(ReadInfoRec));
  if (!ce->ce_dataChunks) {
    fprintf(stderr, "error allocating new datacache rir\n");
    return(TRUE);
  }

  ce->ce_lastValidation = globalTimeOfDay.tv_sec;

  hashBin = HashFilename(ce->ce_filename);
  ce->ce_prevInHash = &cacheBins[hashBin];
  ce->ce_nextInHash = cacheBins[hashBin];
  if (cacheBins[hashBin]) 
    cacheBins[hashBin]->ce_prevInHash = &ce->ce_nextInHash;
  cacheBins[hashBin] = ce;

  permStats.ps_numDataCacheEntries++;
  
  return(FALSE);
}
/* ------------------------------------------------------------------*/
static DelayedUnmap *
TryFreeChunk(CacheEntry *ent, int chunkNum, int doDelay)
{
  void *data;
  int chunkSize;
  DelayedUnmap *dumap = NULL;

  if (ent->ce_dataChunks[chunkNum].rir_numRefs) {
    fprintf(stderr, "tried freeing chunk with refs\n");
    exit(-1);
  }

  data = ent->ce_dataChunks[chunkNum].rir_data;
  if (!data)
    return(NULL);
  ent->ce_dataChunks[chunkNum].rir_data = NULL;

  RemoveFromChunkLRUList(ent, chunkNum);

  chunkSize = GetRoundedChunkSize(ent, chunkNum);

  if (doDelay) {
    dumap = calloc(1, sizeof(DelayedUnmap));
    if (!dumap)
      Panic("dumap failed");
    dumap->du_addr = data;
    dumap->du_len = chunkSize;
  }
  else if (munmap(data, chunkSize) < 0) 
    fprintf(stderr, "munmap - %d\n", errno);

  bytesInAllChunks -= GetRoundedChunkSize(ent, chunkNum);
  ent->ce_numChunksInMem--;
  if (ent->ce_numChunksInMem < 0) {
    fprintf(stderr, "negative chunks in mem\n");
    exit(-1);
  }
  return(dumap);
}
/* ------------------------------------------------------------------*/
static void
KillEntry(CacheEntry *ent) 
{
  int i, numChunks;

  /* remove from whatever list it's on */
  if (ent->ce_nextInHash)
    ent->ce_nextInHash->ce_prevInHash = ent->ce_prevInHash;
  *ent->ce_prevInHash = ent->ce_nextInHash;

  numChunks = ent->ce_numChunks;
  for (i = 0; i < numChunks; i++)
    TryFreeChunk(ent, i, FALSE);

  if (ent->ce_file_fd >= 0) {
    if (close(ent->ce_file_fd))
      perror("close");
    ent->ce_file_fd = -1;
  }

  if (ent->ce_respHeaderLen) {
    free(ent->ce_respHeader);
    ent->ce_respHeader = NULL;
    ent->ce_respHeaderLen = 0;
  }

  if (ent->ce_encodings)
    free(ent->ce_encodings);
  if (ent->ce_dataChunks)
    free(ent->ce_dataChunks);
  if (ent->ce_filename)
    free(ent->ce_filename);

  permStats.ps_numDataCacheEntries--;
  free(ent);
}
/* ------------------------------------------------------------------*/
static void 
MakeEntryInvalid(CacheEntry *ent)
{
  /* if we don't have references, dispose */
  if (!ent->ce_numRefs) {
    KillEntry(ent);
    return;
  }

  /* remove from hash list */
  if (ent->ce_nextInHash)
    ent->ce_nextInHash->ce_prevInHash = ent->ce_prevInHash;
  *ent->ce_prevInHash = ent->ce_nextInHash;

  /* store on invalid list */
  ent->ce_nextInHash = invalidEntriesHead;
  ent->ce_prevInHash = &invalidEntriesHead;
  if (invalidEntriesHead)
    invalidEntriesHead->ce_prevInHash = &ent->ce_nextInHash;

  invalidEntriesHead = ent;
  ent->ce_isInvalid = TRUE;
}
/* ------------------------------------------------------------------*/
CacheEntry *
CheckCache(char *exName)
{
  CacheEntry *walk;
  int hashBin;
  int numWalked = 0;

  mainStats.cs_numDataCacheChecks++;
  hashBin = HashFilename(exName);

  for (walk = cacheBins[hashBin]; walk; walk = walk->ce_nextInHash) {
    numWalked++;
    if (strcmp(walk->ce_filename, exName))
      continue;

    /* check to see if we need to revalidate entry */
    if ((globalTimeOfDay.tv_sec - walk->ce_lastValidation) > 
	FILECACHEVALIDATETIME) {
      struct stat newInfo;
      if ((stat(walk->ce_filename, &newInfo) < 0)  ||
	  (newInfo.st_mtime != walk->ce_modTime)) {
	/* if stat fails or if validation fails, kill this entry 
	   also, don't continue walking - chain may have changed,
	   and there's nothing that will match anyway */
	MakeEntryInvalid(walk);

	mainStats.cs_numDataCacheWalks += numWalked;
	return(NULL);
      }
      walk->ce_lastValidation = globalTimeOfDay.tv_sec;
    }
    walk->ce_numRefs++;

    if (walk != cacheBins[hashBin]) {	/* move this entry to front of list */
      /* remove from current position */
      if (walk->ce_nextInHash)
	walk->ce_nextInHash->ce_prevInHash = walk->ce_prevInHash;
      *walk->ce_prevInHash = walk->ce_nextInHash;
      
      /* add to front */
      walk->ce_prevInHash = &cacheBins[hashBin];
      walk->ce_nextInHash = cacheBins[hashBin];
      if (cacheBins[hashBin]) 
	cacheBins[hashBin]->ce_prevInHash = &walk->ce_nextInHash;
      cacheBins[hashBin] = walk;
    }
    
    mainStats.cs_numDataCacheHits++;
    mainStats.cs_numDataCacheWalks += numWalked;
    return(walk);
  }
  mainStats.cs_numDataCacheWalks += numWalked;
  return(NULL);
}
/* ------------------------------------------------------------------*/
CacheEntry *
GetEmptyCacheEntry(void)
{
  return(calloc(1, sizeof(CacheEntry)));
}
/* ------------------------------------------------------------------*/
void 
ReleaseCacheEntry(CacheEntry **entP)
{
  CacheEntry *ent;

  ent = *entP;
  ent->ce_numRefs--;
  if (ent->ce_numRefs < 1) {
    if (ent->ce_file_fd >= 0) {
      if (close(ent->ce_file_fd))
	perror("close");
      ent->ce_file_fd = -1;
    }
    if (ent->ce_isInvalid)
      KillEntry(ent);
  }
  *entP = NULL;
}
/* ------------------------------------------------------------------*/
#define MAXMIMESIZE 256		/* make a multiple of 32 to speed checksumming */
/* ------------------------------------------------------------------*/
int
CalcRespHeader(CacheEntry *ce, int status, char* title)
{
  /* this function is basically a variant of send_mime, but we
     don't send anything - we just calculate what we would send,
     and then we hold that info */
  char *encHead;
  char *encParams;
  char tbuf[100];
  char *spaceStart;
  char padBuf[32];
  int doPadding = 0;
  char conLenInfo[40];

  if (ce->ce_respHeader)
    spaceStart = ce->ce_respHeader;
  else
    spaceStart = ce->ce_respHeader = malloc(MAXMIMESIZE);
  if (!spaceStart)
    return(TRUE);
  if (ce->ce_size > 3000)
    doPadding = TRUE;

  MakeHTTPDate(tbuf, ce->ce_modTime);

  /* encoding information may or may not exist -
     this hack is used to conditionally create the encoding line */
  if (ce->ce_encodings[0] == '\0') 
    encHead = encParams = "";
  else {
    encHead = "\nContent-Encoding: ";
    encParams = ce->ce_encodings;
  }

  if (ce->ce_size >= 0) 
    sprintf(conLenInfo, "\nContent-Length: %d", ce->ce_size);
  else 
    conLenInfo[0] = '\0';

  padBuf[0] = 0;
  do {
    /* on first pass, use an empty padding buffer, and on the
       second pass, add in necessary spaces */
    sprintf(spaceStart, 
	    "HTTP/1.0 %d %s\n"
	    "Date: %s\n"
	    "Server: %s%s%s%s\n"
	    "Content-Type: %s%s\n"
	    "Last-Modified: %s\n\n",
	    status, title, 
	    globalTimeOfDayStr, /* will get updated on each use */
	    SERVER_SOFTWARE, padBuf,
	    encHead, encParams,
	    ce->ce_type, conLenInfo,
	    tbuf);

    ce->ce_respHeaderLen = strlen(spaceStart);
    if (ce->ce_respHeaderLen >= MAXMIMESIZE) {
      fprintf(stderr, "panic - MAXMIMESIZE is %d, line was %d\n", 
	      MAXMIMESIZE, ce->ce_respHeaderLen);
      exit(-1);
    }
    if (doPadding && (ce->ce_respHeaderLen % 32)) {
      int i;
      int needed = 32 - (ce->ce_respHeaderLen % 32);
      for (i = 0; i < needed; i++)
	padBuf[i] = ' ';
      padBuf[needed] = 0;
    }
  } while (doPadding && (ce->ce_respHeaderLen % 32));
  return(FALSE);
}
/* ------------------------------------------------------------------*/
