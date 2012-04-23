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




#ifndef _LIBHTTPD_H_
#define _LIBHTTPD_H_
#include <sys/time.h>
#include <sys/resource.h>

/* this structure gets zeroed out after each printing */
typedef struct {
  int cs_numAccepts;
  int cs_numRequests;
  int cs_numReqsNeedingReads;
  int cs_numReqsWithMincoreMisses;
  int cs_numDataCacheChecks;
  int cs_numDataCacheHits;
  int cs_numDataCacheWalks;
  int cs_numHotNameChecks;
  int cs_numHotNameHits;
  int cs_numHotNameWalks;
  double cs_bytesSent;		/* not including headers */
  double cs_numPagesSent;	/* pages involved in file xfers */
  double cs_numSelects;		/* only real selects */
  double cs_totSelectsReady;
  int cs_numConvBlocks;		/* converts seen by the threads */
  int cs_numActualConvs;	/* # convs going to the slaves */
  int cs_numReadBlocks;		/* read blocks seen by the master */
  int cs_numActualReads;	/* successful read blocks from slaves */
  int cs_numReadsFailed;	/* failed read blocks from slaves */
  int cs_totRealReadPages;	/* pages involved in real reads */
  double cs_totRealReadBytes;	/* bytes involved in real reads */
  int cs_numActualDirs;		/* number of times directory helpers called */
} ConnStats;

extern ConnStats mainStats;

/* this structure doesn't lose its values after each printing */
typedef struct {
  int ps_numDataCacheEntries;	/* number of files in data cache */
  struct rusage ps_lastUsage;	/* usage info from last printout */
} PermStats;

extern PermStats permStats;

struct httpd_conn;		/* declared for convenience */

/* The httpd structs. */

typedef struct {
  char* hostname;
  int port;
  char* cgi_pattern;
  char* cwd;
  int fd;
} httpd_server;

extern httpd_server HS;

#include <sys/time.h>
extern struct timeval globalTimeOfDay;
extern char globalTimeOfDayStr[];
void MakeHTTPDate(char *buf, time_t unixTime);

float DiffTime(struct timeval *start, struct timeval *end);



/* Initializes.  Does the socket(), bind(), and listen().   Returns an
** httpd_server* which includes a socket fd that you can select() on.
** Return (httpd_server*) 0 on error.
*/
extern int HttpdInitialize(char* hostname, int port, char* cgi_pattern, 
			   char* cwd);

/* Call to shut down. */
extern void HttpdTerminate(void);

/* Starts sending data back to the client.  In some cases (directories,
** CGI programs), finishes sending by itself - in those cases, return value
** is SR_DONOTHING.  If there is more data to be sent, 
** then returns SR_READY
**
** Returns SR_ERROR on error.
*/

/* Send an error message back to the client. */
void HttpdSendErr(struct httpd_conn* hc, int status, char* title, 
		  char* form, char* arg);

/* Some error messages. */
extern char* httpd_err503title;
extern char* httpd_err503form;

/* Generates a string representation of a method number. */
extern char* HttpdMethodStr(int method);

/* Sets the allowed number of file descriptors to the maximum and returns it. */
extern int HttpdGetNFiles(void);


extern int defaultSendBufSize;

/* ---------------------------------------------------------------- */


extern char *ok200title;
extern char *err302title;
extern char *err302form;
extern char *err304title;
extern char *err400title;
extern char *err400form;
extern char *err403title;
extern char *err403form;
extern char *err404title;
extern char *err404form;
extern char *err408title;
extern char *err408form;
extern char *err500title;
extern char *err500form;
extern char *err501title;
extern char *err501form;
extern char *httpd_err503title;
extern char *httpd_err503form;

extern void SendMime(struct httpd_conn* hc, int status, char* title, 
		      char* encodings, char* extraheads, char* type, 
		      int length, time_t mod);

char *realloc_str(char** strP, int* maxsizeP, int size);


extern char *methodStrings[];

void SendDirRedirect(struct httpd_conn* hc);

extern int systemPageSize;
extern int systemPageSizeBits;

extern int GenericStringHash(unsigned char *string);


#endif /* _LIBHTTPD_H_ */
