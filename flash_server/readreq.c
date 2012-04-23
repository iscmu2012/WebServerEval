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
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>	/* for tcp_nodelay option */

#include "libhttpd.h"		/* for definition of httpd_conn, etc */
#include "readreq.h"
#include "tdate_parse.h"
#include "conn.h"
#include "handy.h"
#include "common.h"
#include "config.h"

/* ---------------------------------------------------------------- */
static int 
ProcessFirstHeaderLine(httpd_conn* hc, char *buf)
{
  char* method_str;
  char* url;
  char* protocol;
  char* eol;
  int i;

  /* Parse request. */
  hc->hc_mimeFlag = 0;
  method_str = buf;
  url = strpbrk(method_str, " \t\n\r");
  if (url == (char*) 0) {
    HttpdSendErr(hc, 400, err400title, err400form, buf);
    return -1;
  }
  *url++ = '\0';
  url += strspn(url, " \t\n\r");
  protocol = strpbrk(url, " \t\n\r");
  if (protocol == (char*) 0)
    protocol = "HTTP/0.9";
  else {
    *protocol++ = '\0';
    protocol += strspn(protocol, " \t\n\r");
    if (*protocol != '\0') {
      hc->hc_mimeFlag = 1;
      eol = strpbrk(protocol, " \t\n\r");
      if (eol != (char*) 0)
	*eol = '\0';
    }
  }
  
  for (i = 0; i < NUM_METHODS; i++) {
    if (strcmp(method_str, methodStrings[i]) == 0) {
      hc->hc_method = i;
      break;
    }
  }
  
  hc->hc_encodedurl = url;
  hc->hc_protocol = protocol;
  
  return(0);
}
/* ---------------------------------------------------------------- */

typedef enum HeaderEnums { HO_IGNORE, HO_PERSISTENT, HO_CONTENTLENGTH,
  HO_CONTENTTYPE, HO_COOKIE, HO_IMSGET, HO_ACCEPTENCODING, HO_ACCEPT,
  HO_USERAGENT, HO_REFERER
} HeaderEnums;

typedef struct HeaderOption {
  char *ho_name;
  HeaderEnums ho_enum;
  int ho_nameLen;
  struct HeaderOption *ho_next;
  int ho_hashVal;
} HeaderOption;

/* add most common entries to the end of this list */
HeaderOption headerOptions[] = {
  {"Accept-Charset:", HO_IGNORE},
  {"Accept-Language:", HO_IGNORE},
  {"Authorization:", HO_IGNORE},
  {"Cache-Control:", HO_IGNORE},
  {"Cache-Info:", HO_IGNORE},
  {"Charge-To:", HO_IGNORE},
  {"Client-ip:", HO_IGNORE},
  {"Connection:", HO_IGNORE},
  {"Date:", HO_IGNORE},
  {"Extension:", HO_IGNORE},
  {"Forwarded:", HO_IGNORE},
  {"From:", HO_IGNORE},
  {"Host:", HO_IGNORE},
  {"HTTP-Version:", HO_IGNORE},
  {"Message-ID:", HO_IGNORE},
  {"MIME-Version:", HO_IGNORE},
  {"Pragma:", HO_IGNORE},
  {"Proxy-agent:", HO_IGNORE},
  {"Proxy-Connection:", HO_IGNORE},
  {"Security-Scheme:", HO_IGNORE},
  {"Session-ID:", HO_IGNORE},
  {"UA-color:", HO_IGNORE},
  {"UA-CPU:", HO_IGNORE},
  {"UA-Disp:", HO_IGNORE},
  {"UA-OS:", HO_IGNORE},
  {"UA-pixels:", HO_IGNORE},
  {"User:", HO_IGNORE},
  {"Via:", HO_IGNORE},
  {"X-P:", HO_PERSISTENT},	/* done to reduce size of header to avoid TCP bug */
  {"X-Persistent:", HO_PERSISTENT},
  /* {"Connection:", HO_APACHEPERSIST}, */
  {"Content-Length:", HO_CONTENTLENGTH},
  {"Content-Type:", HO_CONTENTTYPE},
  {"Cookie:", HO_COOKIE},
  {"If-Modified-Since:", HO_IMSGET},
  {"Accept-Encoding:", HO_ACCEPTENCODING},
  {"Accept:", HO_ACCEPT},
  {"User-Agent:", HO_USERAGENT},
  {"Referer:", HO_REFERER},
  {NULL}};


#define HEADERLINEBINS 64	/* must be power of 2 */
static HeaderOption *headerBins[HEADERLINEBINS];
static char charConversions[256];

/* ---------------------------------------------------------------- */
static void
DoHeaderLinesSetup(void)
{
  /* add entries to head of list so that last entries
     show up first in the search */
  HeaderOption *entry;
  int pos;

  for (pos = 'a'; pos <= 'z'; pos++) 
    charConversions[pos] = pos - 'a';
  for (pos = 'A'; pos <= 'Z'; pos++) 
    charConversions[pos] = pos - 'A';

  for (entry = headerOptions; entry->ho_name; entry++) {
    int hashVal = 0;
    for (pos = 0; entry->ho_name[pos]; pos++) {
      unsigned char c;
      if (hashVal & 1)
	hashVal ^= HEADERLINEBINS;
      hashVal >>= 1;
      c = entry->ho_name[pos];
      hashVal ^= charConversions[c];
    }
    entry->ho_nameLen = strlen(entry->ho_name);
    entry->ho_hashVal = hashVal;
    entry->ho_next = headerBins[hashVal & (HEADERLINEBINS-1)];
    headerBins[hashVal & (HEADERLINEBINS-1)] = entry;
  }
}
/* ---------------------------------------------------------------- */
static int 
ProcessRemainingHeaderLines(httpd_conn* hc, char *buf)
{
  int sz;
    char* cp;
  static int doHeaderSetup = TRUE;
  int hashVal;
  int pos;
  unsigned char c;
  HeaderOption *entry;

  if (doHeaderSetup) {
    doHeaderSetup = FALSE;
    DoHeaderLinesSetup();
  }

  /* Read any other headers in the request, particularly referer. */
  sz = strlen(buf);
  while (sz > 0 && (buf[sz-1] == '\n' || buf[sz-1] == '\r'))
    buf[--sz] = '\0';
  if (sz == 0)
    return(TRUE);		/* done with processing the header */

  hashVal = 0;
  for (pos = 0; pos < sz; pos++) {
    c = buf[pos];
    if (hashVal & 1)
      hashVal ^= HEADERLINEBINS;
    hashVal >>= 1;
    hashVal ^= charConversions[c];
    if (c == ':') {
      pos++;
      break;
    }
  } 

  while (buf[pos] == '\t' || buf[pos] == ' ')
    pos++;
  cp = &buf[pos];

  for (entry = headerBins[hashVal & (HEADERLINEBINS-1)];
       entry; entry = entry->ho_next) {
    /* first check hashval, then check for string match */
    if ((entry->ho_hashVal == hashVal) && 
	(strncasecmp(buf, entry->ho_name, entry->ho_nameLen)==0))
      break;
  }

  if (!entry) {
    /* either we have an error or an X- value */
    if (strncasecmp(buf, "X-", 2) != 0)
      fprintf(stderr, "unknown MIME header: %.80s\n", buf);
    return(FALSE);		/* ignore, keep processing */
  }

  switch(entry->ho_enum) {
  case HO_COOKIE:
    hc->hc_cookie = cp;
    break;
  case HO_CONTENTTYPE:
    hc->hc_contentType = cp;
    break;
  case HO_REFERER:
    hc->hc_referer = cp;
    break;
  case HO_USERAGENT:
    hc->hc_userAgent = cp;
    break;
  case HO_ACCEPT:
    if (hc->hc_accept[0] != '\0') {
      if (strlen(hc->hc_accept) > 5000) {
	fprintf(stderr, "%.80s way too much Accept: data",
		inet_ntoa(hc->hc_clientAddr));
	return(FALSE);		/* keep processing */
      }
      if (!realloc_str(&hc->hc_accept, &hc->hc_maxAccept,
		       strlen(hc->hc_accept) + 2 + strlen(cp))) {
	fprintf(stderr, "couldn't alloc more for accept header\n");
	return(FALSE);
      }
      strcat(hc->hc_accept, ", ");
    }
    else {
      if (!realloc_str(&hc->hc_accept, &hc->hc_maxAccept, strlen(cp))) {
	fprintf(stderr, "couldn't allocate accept header\n");
	return(FALSE);
      }
    }
    strcat(hc->hc_accept, cp);
    break;
  case HO_ACCEPTENCODING:
    if (hc->hc_accepte[0] != '\0') {
      if (strlen(hc->hc_accepte) > 5000) {
	fprintf(stderr, "%.80s way too much Accept-Encoding: data",
	       inet_ntoa(hc->hc_clientAddr));
	return(FALSE);		/* keep processing */
      }
      if (!realloc_str(&hc->hc_accepte, &hc->hc_maxAccepte,
		       strlen(hc->hc_accepte) + 2 + strlen(cp))) {
	fprintf(stderr, "couldn't allocate more for accept-enc header\n");
	return(FALSE);
      }
      strcat(hc->hc_accepte, ", ");
    }
    else {
      if (!realloc_str(&hc->hc_accepte, &hc->hc_maxAccepte, strlen(cp))) {
	fprintf(stderr, "couldn't allocate accept-enc header\n");
	return(FALSE);
      }
    }
    strcat(hc->hc_accepte, cp);
    break;
  case HO_IMSGET:
    hc->hc_ifModifiedSince = tdate_parse(cp);
    if (hc->hc_ifModifiedSince == (time_t) -1) 
      fprintf(stderr, "unparsable time: %.80s", cp);
    break;
  case HO_CONTENTLENGTH:
    hc->hc_contentLength = atoi(cp);
    if (hc->hc_contentLength < 0)
      hc->hc_contentLength = -1;
    break;
  case HO_PERSISTENT:
    hc->hc_isPersistentConnection = atoi(cp);
    /* shut off nagle's algorithm */

    if (!hc->hc_nagleOff) {
      int nagleToggle = 1;
      hc->hc_nagleOff = TRUE;
      if (setsockopt(hc->hc_fd, IPPROTO_TCP, TCP_NODELAY, 
		     (char *) &nagleToggle, sizeof(nagleToggle)) < 0) 
	fprintf(stderr, "failed to disable nagle\n");
    }
    break;
  case HO_IGNORE:
    break;
  default:
    fprintf(stderr, "unknown HeaderEnum code: %d\n", entry->ho_enum);
    break;
  }

  return(FALSE);		/* keep on processing the header */
}

/* ---------------------------------------------------------------- */
void
ReleaseHeaderInfo(HeaderInfoStruct **hisP, int connClosing)
{
  int i;
  int unsent;
  int num;
  int allDataProcessed;
  HeaderInfoStruct *his;

  his = *hisP;

  his->his_rs = RS_FIRST;

  num = his->his_numBuffers;
  unsent = his->his_unsentBufNum;

  /* free all strings */
  for (i = 0; i < his->his_numExtraStrings; i++)
    free(his->his_extraStrings[i]);
  free(his->his_extraStrings);
  his->his_extraStrings = NULL;
  his->his_numExtraStrings = 0;

  allDataProcessed = 
    (his->his_unsentBufNum == his->his_availBufNum) &&
      (his->his_unsentPos == his->his_availPos);

  if (connClosing || allDataProcessed) {
    /* free all but first buffer, but # buffers stays the same */
    for (i = 1; i < num; i++) {
      if (his->his_bufs[i].rrb_buf) {
	free(his->his_bufs[i].rrb_buf);
	his->his_bufs[i].rrb_buf = NULL;
      }
    }
    
    his->his_unsentBufNum = his->his_unsentPos =
      his->his_availBufNum =  his->his_availPos = 0;
  }
  else {
    if (unsent) {
      /* free any processed buffers */
      for (i = 0; i < unsent; i++) {
	if (his->his_bufs[i].rrb_buf) {
	  free(his->his_bufs[i].rrb_buf);
	  his->his_bufs[i].rrb_buf = NULL;
	}
      }

      /* shift remaining buffers */
      for (i = unsent; i < num; i++)
	his->his_bufs[i-unsent] = his->his_bufs[i];
      his->his_unsentBufNum = 0;
      his->his_availBufNum -= unsent;

      /* clear rest */
      for (i = 1+his->his_availBufNum; i < num; i++)
	his->his_bufs[i].rrb_buf = NULL;
    }
  }
}
/* ---------------------------------------------------------------- */
static int
InitHIS(httpd_conn* hc, int *retVal)
{
  HeaderInfoStruct *his;
  RequestReadBuf *rrb;

  his = hc->hc_headerInfo = calloc(1, sizeof(HeaderInfoStruct));
  if (!his) {
    fprintf(stderr, "out of memory allocating his\n");
    *retVal = PRR_ERROR;
    return(TRUE);
  }
  rrb = his->his_bufs = calloc(1, sizeof(RequestReadBuf));
  if (!his->his_bufs) {
    fprintf(stderr, "out of memory allocating his_bufs\n");
    *retVal = PRR_ERROR;
    return(TRUE);
  }
  his->his_numBuffers = 1;
  rrb->rrb_buf = malloc(PRR_BUFSIZE);
  if (!rrb->rrb_buf) {
    fprintf(stderr, "out of memory allocating rrb_buf\n");
    *retVal = PRR_ERROR;
    return(TRUE);
  }
  rrb->rrb_size = PRR_BUFSIZE;
  his->his_rs = RS_FIRST;

  return(FALSE);
}
/* ---------------------------------------------------------------- */
static int
GetNewBuffer(HeaderInfoStruct *his, int *retVal)
{
  RequestReadBuf *rrb;

  his->his_availPos = 0;
  his->his_availBufNum++;
  if (his->his_availBufNum == his->his_numBuffers) {
    /* allocate a new buffer pointer */
    his->his_numBuffers++;
    rrb = realloc(his->his_bufs, 
		  his->his_numBuffers * sizeof(RequestReadBuf));
    if (!rrb) {
      his->his_numBuffers--;
      fprintf(stderr, "out of memory re-allocating rrb_buf\n");
      *retVal = PRR_ERROR;
      return(TRUE);
    }
    his->his_bufs = rrb;
    rrb[his->his_availBufNum].rrb_buf = NULL;
  }
  
  rrb = &his->his_bufs[his->his_availBufNum];
  
  /* if we don't have space, allocate new buffer */
  if (rrb->rrb_buf == NULL) {
    rrb->rrb_buf = malloc(PRR_BUFSIZE);
    if (!rrb->rrb_buf) {
      fprintf(stderr, "out of memory allocating new rrb_buf\n");
      *retVal = PRR_ERROR;
      return(TRUE);
    }
    rrb->rrb_size = PRR_BUFSIZE;
  }

  return(FALSE);
}
/* ---------------------------------------------------------------- */
static int
MakeCrossedString(HeaderInfoStruct *his, int *retVal,
		  char **resString, int *resLen)
{
  int i;
  int size;
  char *newString;
  int firstBufLen;
  char *tempString;
  RequestReadBuf *rrb;
  char **newExtra;

  /* start with # of bytes in first buffer */
  firstBufLen = size = his->his_bufs[his->his_unsentBufNum].rrb_size - 
    his->his_unsentPos;

  /* add number of bytes in last buffer */
  size += his->his_availPos;

  /* walk through intermediate buffers, add their sizes */
  for (i = his->his_unsentBufNum+1; i < his->his_availBufNum; i++)
    size += his->his_bufs[i].rrb_size;

  *resLen = size;
  newString = malloc(size);
  if (!newString) {
    fprintf(stderr, "out of memory allocating crossed string\n");
    *retVal = PRR_ERROR;
    return(TRUE);
  }

  his->his_numExtraStrings++;
  newExtra = realloc(his->his_extraStrings, 
		     his->his_numExtraStrings * sizeof(char *));
  if (!newExtra) {
    his->his_numExtraStrings--;
    free(newString);
    fprintf(stderr, "out of memory re-allocating his_extraStrings\n");
    *retVal = PRR_ERROR;
    return(TRUE);
  }

  his->his_extraStrings = newExtra;
  his->his_extraStrings[his->his_numExtraStrings-1] = newString;

  tempString = newString;

  /* first copy over partial buf */
  memcpy(tempString,
	 &his->his_bufs[his->his_unsentBufNum].rrb_buf[his->his_unsentPos], 
	 firstBufLen);
  tempString += firstBufLen;

  /* copy intermediate bufs */
  for (i = his->his_unsentBufNum+1; i < his->his_availBufNum; i++) {
    rrb = &his->his_bufs[i];
    memcpy(tempString, rrb->rrb_buf, rrb->rrb_size);
    tempString += rrb->rrb_size;
  }

  /* copy final buf */
  memcpy(tempString, &his->his_bufs[his->his_availBufNum].rrb_buf,
	 his->his_availPos);

  *resString = newString;

  return(FALSE);
}
/* ---------------------------------------------------------------- */
enum PRRTypes
ProcessRequestReading(httpd_conn* hc)
{
  /*
     allocate if we need space
     read in next buffer
     if we're processing headers, put together next string and send
     if we're just reading content data, keep track of it
     */
  HeaderInfoStruct *his;
  RequestReadBuf *rrb;
  int readStart, readEnd;
  char *buf;
  int i;
  int retVal = PRR_DONE;
  int res;

  his = hc->hc_headerInfo;
  if (!his) {
    if (InitHIS(hc, &retVal))
      return(retVal);
    his = hc->hc_headerInfo;
  }

  /* if the buffer for reading is full, get a new one */
  if (his->his_availPos == his->his_bufs[his->his_availBufNum].rrb_size) {
    if (GetNewBuffer(his, &retVal))
      return(retVal);
  }

  /* read in next piece of input */
  rrb = &his->his_bufs[his->his_availBufNum];
  res = read(hc->hc_fd, &rrb->rrb_buf[his->his_availPos], 
	     rrb->rrb_size - his->his_availPos);

  if (res < 0) {
    if (errno == EAGAIN)
      return(PRR_READMORE);
    return(PRR_ERROR);
  }
  if (res == 0)
    return(PRR_CLOSED);

  readStart = his->his_availPos;
  his->his_availPos += res;
  readEnd = his->his_availPos;

  if (his->his_rs == RS_DATA) {
    his->his_unsentBufNum = his->his_availBufNum;

    if (res >= his->his_conDataLeft) {
      /* if we've read enough, stop */
      his->his_unsentPos = his->his_availPos - (res - his->his_conDataLeft);
      his->his_conDataLeft = 0;
      return(PRR_DONE);
    }
    his->his_conDataLeft -= res;
    his->his_unsentPos = his->his_availPos;
    return(PRR_READMORE);
  }

  buf = rrb->rrb_buf;
  /* process all the newlines in this batch */
  for (i = readStart; i < readEnd; i++) {
    char *string;
    int stringLen;
    if (buf[i] != '\n')
      continue;

    buf[i] = 0;

    /* determine if the string is simple or crosses buffers */
    if (his->his_availBufNum != his->his_unsentBufNum) {
      his->his_unsentPos = i+1;
      if (MakeCrossedString(his, &retVal, &string, &stringLen))
	return(retVal);
      his->his_unsentBufNum = his->his_availBufNum;
    }
    else {
      string = &buf[his->his_unsentPos];
      stringLen = i - his->his_unsentPos;
      his->his_unsentPos = i+1;
    }
    
    if (his->his_rs == RS_REST) {
      if (ProcessRemainingHeaderLines(hc, string)) {
	if (hc->hc_contentLength < 1) /* no other data */
	  return(PRR_DONE);

	his->his_conDataStartBuf = his->his_unsentBufNum;
	his->his_conDataStartPos = his->his_unsentPos;

	/* if we've read in all content data, we're done */
	if (his->his_availPos - his->his_unsentPos >= hc->hc_contentLength) {
	  his->his_unsentPos += hc->hc_contentLength;
	  return(PRR_DONE);
	}
	
	his->his_rs = RS_DATA;
	his->his_conDataLeft = hc->hc_contentLength - 
	  (his->his_availPos - his->his_unsentPos);
	his->his_unsentPos = his->his_availPos;
	return(PRR_READMORE);
      }
    }
    else if (his->his_rs == RS_FIRST) {

      /* if we're logging, copy the first line */
      if (accessLoggingEnabled) {
	if (stringLen+1 > hc->hc_maxOrigFirstLine) {
	  if (!realloc_str(&hc->hc_origFirstLine, &hc->hc_maxOrigFirstLine, 
			   stringLen+1)) {
	    fprintf(stderr, "couldn't realloc copy of first line\n");
	    return(PRR_ERROR);
	  }
	}
	memcpy(hc->hc_origFirstLine, string, stringLen+1);
	if (stringLen > 0 && hc->hc_origFirstLine[stringLen-1] == '\r') {
	  hc->hc_origFirstLine[stringLen-1] = '\0';
	  stringLen--;
	}
	hc->hc_origFirstLineLen = stringLen;
      }

      his->his_rs = RS_REST;
      if (ProcessFirstHeaderLine(hc, string) < 0)
	return(PRR_ERROR);
      if (!hc->hc_mimeFlag)
	return(PRR_DONE);
    }
  }
  return(PRR_READMORE);
}
/* ---------------------------------------------------------------- */
struct iovec *
MakeContentDataIOV(httpd_conn* hc, int *numElements)
{
  /* generate an iov array for all of the contentdata buffers */
  HeaderInfoStruct *his;
  int i;
  int j;
  struct iovec *ret;

  his = hc->hc_headerInfo;
  *numElements = 1 + his->his_unsentBufNum - his->his_conDataStartBuf;

  ret = malloc(*numElements * sizeof(struct iovec));
  if (!ret)
    return(NULL);

  ret[0].iov_base = his->his_bufs[his->his_conDataStartBuf].rrb_buf +
    his->his_conDataStartPos;

  if (*numElements == 1) {
    ret[0].iov_len = his->his_unsentPos - his->his_conDataStartPos;
  }
  else {
    /* set up first buffer and last buffer, then walk middle buffers*/

    ret[0].iov_len = his->his_bufs[his->his_conDataStartBuf].rrb_size -
      his->his_conDataStartPos;
    j = *numElements-1;
    ret[j].iov_base = his->his_bufs[his->his_unsentBufNum].rrb_buf;
    ret[j].iov_len = his->his_unsentPos;
    
    for (i = his->his_conDataStartBuf+1, j = 1; 
	 i < his->his_unsentBufNum; 
	 i++, j++) {
      ret[j].iov_base = his->his_bufs[i].rrb_buf;
      ret[j].iov_len = his->his_bufs[i].rrb_size;
    }
  }
  return(ret);
}
/* ---------------------------------------------------------------- */
