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
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "libhttpd.h"		/* for realloc_str and globaltimeofday */
#include "hotname.h"
#include "handy.h"
#include "cgi.h"
#include "common.h"
#include "config.h"

typedef struct HotBin {
  HotNameEntry *hb_head;
  time_t hb_time;
} HotBin;

#define HOTNAMEBINS 1024	/* change hash function if this changes */
#define HOTNAMEBINSBITS 10	/* log2(HOTNAMEBINS) */
static HotBin hotNames[HOTNAMEBINS];
#define HOTCGIBINS 256
static HotBin hotCGIs[HOTCGIBINS];

static int numTotalHNEs = 0;

/* only entries without any refs are in
   the MRU list */
static HotNameEntry *headMRU;
static HotNameEntry *tailMRU;

/* ---------------------------------------------------------------- */
void
DumpHotNameStats(void)
{
  int i;
  int lenCounts[21];
  int totalEntries = 0;
  double avgLen = 0;

  for (i = 0; i < 21; i++)
    lenCounts[i] = 0;

  for (i = 0; i < HOTNAMEBINS; i++) {
    int len = 0;
    HotNameEntry *walk;
    for (walk = hotNames[i].hb_head; walk; walk = walk->hne_next)
      len++;
    totalEntries += len;
    avgLen += .5*len*(len+1);
    if (len > 20)
      len = 20;
    lenCounts[len]++;
  }

  fprintf(stderr, "hotname cache lengths (truncated at 20): %d entries total\n",
	  totalEntries);
  if (totalEntries)
    avgLen /= totalEntries;	/* static avg seen by entries */
  for (i = 0; i < 21; i++)
    if (lenCounts[i])
      fprintf(stderr, "length %d: %d\n", i, lenCounts[i]);
  fprintf(stderr, "avg len %.2f\n", avgLen);
}
/* ------------------------------------------------------------------*/
static HotNameEntry *
RemoveFromMRU(HotNameEntry *h)
{
  if (h->hne_refCount) {
    fprintf(stderr, "hne shouldn't be on MRU\n");
    exit(-1);
  }

  if (h->hne_prevMRU)
    h->hne_prevMRU->hne_nextMRU = h->hne_nextMRU;
  else
    headMRU = h->hne_nextMRU;
  if (h->hne_nextMRU)
    h->hne_nextMRU->hne_prevMRU = h->hne_prevMRU;
  else
    tailMRU = h->hne_prevMRU;
  return(h);
}
/* ---------------------------------------------------------------- */
static void
AddToMRU(HotNameEntry *h)
{
  if (h->hne_refCount) {
    fprintf(stderr, "hne shouldn't be added to MRU\n");
    exit(-1);
  }
  
  h->hne_nextMRU = headMRU;
  h->hne_prevMRU = NULL;
  if (headMRU)
    headMRU->hne_prevMRU = h;
  else
    tailMRU = h;
  headMRU = h;
}
/* ---------------------------------------------------------------- */
static int 
HotNameHash(unsigned char *encoded)
{
  int val;

  val = GenericStringHash(encoded);
  val += (val >> HOTNAMEBINSBITS) + (val >> (2*HOTNAMEBINSBITS));
  return(val & (HOTNAMEBINS-1));
}
/* ---------------------------------------------------------------- */
static struct HotNameEntry *
RemoveHotNameEntry(struct HotNameEntry *temp)
{
  /* frees the entry, returns the next entry */
  HotNameEntry *del;

  numTotalHNEs--;

  del = temp;
  temp = temp->hne_next;
  *del->hne_prev = temp;
  if (temp)
    temp->hne_prev = del->hne_prev;
  free(del->hne_encoded);
  free(del->hne_expanded);
  free(del->hne_stripped);
  free(del);
  return(temp);
}
/* ---------------------------------------------------------------- */
static void
RemoveOldEntriesInBin(int bin)
{
  HotNameEntry *entry;

  for (entry = hotNames[bin].hb_head; entry; ) {
    if (entry->hne_refCount == 0 &&
	(globalTimeOfDay.tv_sec - entry->hne_time) > NAMECACHELIFETIME)
      entry = RemoveHotNameEntry(RemoveFromMRU(entry));
    else
      entry = entry->hne_next;
  }

  hotNames[bin].hb_time = globalTimeOfDay.tv_sec;
}
/* ---------------------------------------------------------------- */
HotNameEntry *
FindMatchInHotNameCache(char *matchName)
{
  HotNameEntry *entry;
  int bin;
  int numWalked = 0;

  mainStats.cs_numHotNameChecks++;
  bin = HotNameHash(matchName);
  if (!hotNames[bin].hb_head)
    return(NULL);

  if (globalTimeOfDay.tv_sec - hotNames[bin].hb_time > NAMECACHELIFETIME) 
    RemoveOldEntriesInBin(bin);

  for (entry = hotNames[bin].hb_head; entry; entry = entry->hne_next) {
    numWalked++;
    if (strcmp(matchName, entry->hne_encoded)) 
      continue;

    if ((globalTimeOfDay.tv_sec - entry->hne_time) > NAMECACHELIFETIME) {
      if (entry->hne_refCount == 0) 
	RemoveHotNameEntry(RemoveFromMRU(entry));
      mainStats.cs_numHotNameWalks += numWalked;
      return(NULL);		/* entry is invalid */
    }

    if (!entry->hne_refCount)
      RemoveFromMRU(entry);
    entry->hne_refCount++;
    
    if (entry != hotNames[bin].hb_head) { /* move entry to front */
      /* remove from current position */
      if (entry->hne_next)
	entry->hne_next->hne_prev = entry->hne_prev;
      *entry->hne_prev = entry->hne_next;
      
      /* insert at front */
      if (hotNames[bin].hb_head)
	hotNames[bin].hb_head->hne_prev = &entry->hne_next;
      entry->hne_next = hotNames[bin].hb_head;
      entry->hne_prev = &hotNames[bin].hb_head;
      hotNames[bin].hb_head = entry;
    }
    
    mainStats.cs_numHotNameHits++;
    mainStats.cs_numHotNameWalks += numWalked;
    return(entry);
  }
  mainStats.cs_numHotNameWalks += numWalked;
  return(NULL);
}
/* ---------------------------------------------------------------- */
void 
EnterIntoHotNameCache(HotNameEntry **tempP)
{
  /* always add new entries to the front */
  int bin;
  HotNameEntry *temp;

  temp = *tempP;

  temp->hne_refCount = 1;
  *tempP = NULL;

  numTotalHNEs++;

  bin = HotNameHash(temp->hne_encoded);

  if (hotNames[bin].hb_head)
    hotNames[bin].hb_head->hne_prev = &temp->hne_next;
  temp->hne_next = hotNames[bin].hb_head;
  temp->hne_prev = &hotNames[bin].hb_head;
  hotNames[bin].hb_head = temp;

  temp->hne_time = globalTimeOfDay.tv_sec;
  if (hotNames[bin].hb_time == 0)
    hotNames[bin].hb_time = globalTimeOfDay.tv_sec;
}
/* ---------------------------------------------------------------- */
void 
EnterIntoHotCGICache(HotNameEntry **tempP)
{
  /* always add new entries to the front */
  int bin = 0;
  char *nameWalker;
  HotNameEntry *temp;

  temp = *tempP;

  *tempP = NULL;
  temp->hne_refCount = 1;

  numTotalHNEs++;

  nameWalker = temp->hne_stripped;

  while (*nameWalker) {
    bin <<=1;
    bin += *nameWalker - 'A';
    nameWalker++;
  }
  bin &= (HOTCGIBINS-1);

  if (hotCGIs[bin].hb_head)
    hotCGIs[bin].hb_head->hne_prev = &temp->hne_next;
  temp->hne_next = hotCGIs[bin].hb_head;
  temp->hne_prev = &hotCGIs[bin].hb_head;
  hotCGIs[bin].hb_head = temp;
  
  temp->hne_time = globalTimeOfDay.tv_sec;

  if (hotCGIs[bin].hb_time == 0)
    hotCGIs[bin].hb_time = globalTimeOfDay.tv_sec;
}
/* ---------------------------------------------------------------- */
static HotNameEntry *
TestForCGIMatch(int bin, char *matchName)
{
  HotNameEntry *entry;

  if (!hotCGIs[bin].hb_head)
    return(NULL);

  /* remove all old entries */
  if (globalTimeOfDay.tv_sec - hotCGIs[bin].hb_time > NAMECACHELIFETIME)
    RemoveOldEntriesInBin(bin);
   
  /* test if what we have so far matches anything */
  for (entry = hotCGIs[bin].hb_head; entry; entry = entry->hne_next) {
    if (!strncmp(entry->hne_stripped, matchName, 
		 strlen(entry->hne_stripped))) {
      if ((globalTimeOfDay.tv_sec - entry->hne_time) > NAMECACHELIFETIME)
	return(NULL);		/* entry is invalid */
      return(entry);
    }
  }
  return(NULL);
}
/* ---------------------------------------------------------------- */
HotNameEntry *
FindMatchInHotCGICache(CGIInfo *cg, char *matchName)
{
  /* walk down pathname, see if components match cgi */
  int hashBin = 0;
  char *nameWalker;
  char *query = NULL;
  char *extraInfo = NULL;
  HotNameEntry *entry;

  nameWalker = matchName;

  do {
    char tempChar;

    tempChar = *nameWalker;
    if (tempChar && tempChar != '/' && tempChar != '?') {
      hashBin <<=1;
      hashBin += tempChar - 'A';
      nameWalker++;
      continue;
    }

    entry = TestForCGIMatch(hashBin & (HOTCGIBINS-1), matchName);

    if (!entry) {
      /* if we did a partial path, continue. otherwise,
	 we either had the end or a query, so stop */
      if (tempChar != '/')
	return(NULL);
      hashBin <<=1;
      hashBin += tempChar - 'A';
      nameWalker++;
      continue;
    }

    /* we've got something that's a match - if there's anything after
       this, it's got to be the extra path and/or the query */

    if (tempChar == '?') 
      query = nameWalker+1;
    else if (tempChar == '/') {
      extraInfo = nameWalker+1;
      /* we still might have a query, though */
      query = extraInfo;
      while (*query && *query != '?')
	query++;
      if (*query) {
	*query = 0;
	query++;
      }
      else
	query = NULL;
    }

    *nameWalker = 0;

    /* copy query and pathinfo if we have any */

    cg->cgi_pathInfo[0] = 0;
    if (extraInfo) {
      if (cg->cgi_maxPathInfo < strlen(extraInfo)) {
	if (!realloc_str(&cg->cgi_pathInfo, &cg->cgi_maxPathInfo, 
			 strlen(extraInfo)))
	  return(NULL);
      }
      strcpy(cg->cgi_pathInfo, extraInfo);
    }
    
    if (query) {
      if (cg->cgi_maxQuery < strlen(query)) {
	if (!realloc_str(&cg->cgi_query, &cg->cgi_maxQuery, 
			 strlen(query)))
	  return(NULL);
      }
      strcpy(cg->cgi_query, query);
    }

    if (!entry->hne_refCount)
      RemoveFromMRU(entry);
    entry->hne_refCount++;
    return(entry);

  } while (*nameWalker);

  return(NULL);
}
/* ---------------------------------------------------------------- */
HotNameEntry *
GetEmptyHNE(void)
{
  if (headMRU && numTotalHNEs >= maxNameCacheSize) {
    if (!tailMRU) {
      fprintf(stderr, "hne tail doesn't exist\n");
      exit(-1);
    }
    RemoveHotNameEntry(RemoveFromMRU(tailMRU));
  }
  return(calloc(1, sizeof(HotNameEntry)));
}
/* ---------------------------------------------------------------- */
void 
ReleaseHNE(HotNameEntry **entryP)
{
  HotNameEntry *entry;

  entry = *entryP;

  entry->hne_refCount--;
  if (entry->hne_refCount == 0) {
    if ((globalTimeOfDay.tv_sec - entry->hne_time) > NAMECACHELIFETIME) {
      RemoveHotNameEntry(entry);
    }
    else 
      AddToMRU(entry);
  }
  *entryP = NULL;
}
/* ---------------------------------------------------------------- */

