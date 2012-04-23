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


/*

   library support routines for our cgi stuff

*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>		/* for bzero - used in FD_ZERO */

#include "gscgi.h"

static int envInfo[2];
#define ENVINFO_NUMSTRINGS 1
#define ENVINFO_STRINGSIZE 0
static int acceptSock;

#define IDLETIMEOUT 300		/* shut down after X seconds waiting */

static int contentBytesLeft;

/* ---------------------------------------------------------------- */
void
GSCGI_EstablishConnection(void)
{
  static int alreadyDone = 0;
  char sockName[100];
  struct sockaddr_un saddrun;
  size_t sz;
  int so_sndbuf;
  
  if (alreadyDone)
    return;
  alreadyDone = 1;
  
  /* create an accept socket, write it back */
  
  sprintf(sockName, "/tmp/cgisock_%ld", (long) getpid());
  unlink(sockName);
  
  acceptSock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (acceptSock <0) {
    perror("socket");
    exit(-1);
  }
  
  sz = sizeof(so_sndbuf);
  if (getsockopt(acceptSock, SOL_SOCKET, SO_SNDBUF, (char*) &so_sndbuf, &sz) < 0) {
    fprintf(stderr, "getsockopt SO_SNDBUF\n");
    exit(-1);
  }
  
  sz = sizeof(so_sndbuf);
  if (so_sndbuf < 64*1024) {
    so_sndbuf = 64*1024;
    if (setsockopt(acceptSock, SOL_SOCKET, SO_SNDBUF, (char*) &so_sndbuf, sz) < 0) {
      fprintf(stderr, "setsockopt SO_SNDBUF\n" );
      exit(-1);
    }
  }
  
  saddrun.sun_family = AF_UNIX;
  saddrun.sun_family = AF_UNIX;
  strcpy(saddrun.sun_path, sockName);
  
  if (bind(acceptSock, (struct sockaddr *) &saddrun, sizeof(saddrun))) {
    perror("bind");
    exit(-1);
  }
  
  if (listen(acceptSock, 1)) {
    perror("listen");
    exit(-1);
  }

  if (write(1, sockName, strlen(sockName)+1) < 1) {
    perror("write");
    exit(-1);
  }
}
/* ---------------------------------------------------------------- */
void
GSCGI_AcceptNextRequest(void)
{
  static char *prevEnvStrings = NULL;
  static int newSock;
  static char *envStrings;
  char *tempChar;
  int i;
  static int firstTime = 1;
  struct timeval timeout;
  fd_set readSet;
  int numReady;
  char *contentString;

  if (firstTime) {
    firstTime = 0;

    close(0);
    close(1);
    
    newSock = accept(acceptSock, NULL, 0);
    if (newSock <0) {
      perror("accept");
      exit(-1);
    }
    
    if (newSock > 2) {		/* make stdin, stdout, stderr 
				   be the new socket */	
      dup2(newSock, 0);
      dup2(newSock, 1);
      close(newSock); 
      newSock = 1; 
    }
    else {
      if (newSock != 0)
	dup2(newSock, 0);
      if (newSock != 1)
	dup2(newSock, 1);
    }
  }
  else {
    /* drain remaining content data & tell server we're done */
    CGIChunkHeader ch;

    while (contentBytesLeft) {
      char conBuf[1000];
      int res;

      res = GSCGI_GetContentData(conBuf, sizeof(conBuf));
      if (res <= 0)
	exit(-1);
    }

    ch.ch_size = 0;
    write(1, &ch, sizeof(ch));
  }
  
  timeout.tv_sec = IDLETIMEOUT;
  timeout.tv_usec = 0;
  FD_ZERO(&readSet);
  FD_SET(newSock, &readSet);
  
  numReady = select(newSock+1, &readSet, NULL, NULL, &timeout);
  if (numReady == 0)		/* time limit expired */
    exit(0);

  if (numReady < 0) {
    perror("select");
    exit(-1);
  }

  if (read(newSock, envInfo, sizeof(envInfo)) < sizeof(envInfo)) {
    perror("reading envinfo");
    exit(-1);
  }
  
  envInfo[ENVINFO_STRINGSIZE] -= 2 * sizeof(int);
  envStrings = malloc(envInfo[ENVINFO_STRINGSIZE]);
  if (!envStrings) {
    perror("mallocing envs");
    exit(-1);
  }
  
  if (read(newSock, envStrings, envInfo[ENVINFO_STRINGSIZE]) < 
      envInfo[ENVINFO_STRINGSIZE]) {
    perror("reading envinfo strings");
    exit(-1);
  }

  tempChar = envStrings;
  for (i = 0; i < envInfo[ENVINFO_NUMSTRINGS]; i++) {
    if (putenv(tempChar)) {
      perror("putenv");
      exit(-1);
    }
    tempChar += strlen(tempChar)+1;
  }
  
  if (prevEnvStrings)
    free(prevEnvStrings);
  prevEnvStrings = envStrings;

  contentBytesLeft = 0;
  contentString = getenv("CONTENT_LENGTH");
  if (contentString)
    contentBytesLeft = atoi(contentString);

}
/* ---------------------------------------------------------------- */
int
GSCGI_GetContentData(char *buffer, int maxBytes)
{
  int res;
  int bytesRead = 0;

  if (!contentBytesLeft)
    return(0);

  if (maxBytes > contentBytesLeft)
    maxBytes = contentBytesLeft;

  while (bytesRead < maxBytes) {
    res = read(1, &buffer[bytesRead], maxBytes - bytesRead);
    if (res <= 0) { 
      if (bytesRead)
	return(bytesRead);
      return(res);
    }
    bytesRead += res;
    contentBytesLeft -= res;
  }

  return(bytesRead);
}
/* ---------------------------------------------------------------- */
