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
#include <fcntl.h>
#include <strings.h>
#include <sys/time.h>
#include "config.h"
#include "handy.h"

volatile int junkTouchingInt;

#define OUTFD 0

#ifdef __FreeBSD__
char dumbBuffer[READBLOCKSIZE];
#endif

/* ---------------------------------------------------------------- */
static char *
Touch(int fd, int startPos, int len)
{
  char *addr;
  int i;

  if (lseek(fd, startPos, SEEK_SET) < 0)
    return("lseek failed");

#ifdef __FreeBSD__
  if (read(fd, dumbBuffer, len) != len)
    return("short read");
  return("done");
#else
  addr = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, startPos);
  if (!addr)
    return("mmap failed");

  for (i = 0; i < len; i+= 1024)
    junkTouchingInt += addr[i];

  if (munmap(addr, len) < 0)
    return("unmap failed");
#endif /* FreeBSD */
  return("done");
}
/* ---------------------------------------------------------------- */
void main(int argc, char *argv[])
{
  char request[1024];
  int sendReply = FALSE;
  char *reply = "done";
  int byteStart;
  int readLen;
  char *filename;
  char *temp;
  int fd;
  fd_set rfdset;
  struct timeval selectTimeout;

  if ((argc > 2) && (chdir(argv[2]) < 0)) {
    fprintf(stderr, "failed to switch to slave dir\n");
    exit(-1);
  }

  FD_ZERO(&rfdset);
  selectTimeout.tv_sec = atoi(argv[1]);
  selectTimeout.tv_usec = 0;

  while (1) {
    int ret;
    if (sendReply) {
      write(OUTFD, reply, 1+strlen(reply));
    }
    sendReply = TRUE;
    reply = "done";

    if (selectTimeout.tv_sec) {
      FD_SET(0, &rfdset);
      if (!select(1, &rfdset, NULL, NULL, &selectTimeout)) {
	/* time limit expired - tell master we're idle */
	ret = write(OUTFD, "idle", 5);
	if (ret != 5) {
	  fprintf(stderr, "read slave couldn't write idle - %d\n", ret);
	  perror("write");
	  exit(0);
	}
	sendReply = FALSE;
	continue;
      }
    }

    ret = read(0, request, sizeof(request));
    if (ret == 0)		/* master closed connection */
      exit(0);
    if (ret < 0) {
      fprintf(stderr, "read slave had negative\n");
      exit(0);
    }

    byteStart = atoi(request);
    temp = request;
    while (*temp != ' ')
      temp++;
    temp++;
    readLen = atoi(temp);
    while (*temp != ' ')
      temp++;
    temp++;
    filename = temp;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
      reply = "fd failed";
    else
      reply = Touch(fd, byteStart, readLen);
    close(fd);

  }
}
/* ---------------------------------------------------------------- */
