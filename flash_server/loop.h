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


#ifndef _LOOP_H_
#define _LOOP_H_
struct httpd_conn;

typedef enum DCRCode {DCR_READMORE, DCR_READYFORWRITE, DCR_CLOSED, 
		      DCR_PRIVATE, DCR_NUMCODES} DCRCode;

DCRCode DoConnReadingBackend(struct httpd_conn* c, int fd, int doReqReading);
int DoConnReading(struct httpd_conn* c, int fd, int forRW);
void DoneWithConnection(struct httpd_conn *c, int forceClose);
void ResumeAfterConnReading(struct httpd_conn* c);
void IdleTimeout(struct httpd_conn* c);
void MainLoop(void);
void DisallowNewClients(void);
void InitFDSets(void);

#define MakeConnFDAssociation(hc, fd)  {fdToConnMap[(fd)] = (hc->hc_cnum);}
#define ClearConnFDAssociation(fd) {fdToConnMap[(fd)] = -1;}
typedef int (*SelectHandler)(struct httpd_conn *c, 
			     int fd, int forRW);
void SetSelectHandler(int fd, SelectHandler s, int forRW);

#define SSH_NOTHING 0x0
#define SSH_READ 0x1
#define SSH_WRITE 0x2
extern int fdToConnMap[];

typedef enum {SR_READY, SR_DONOTHING, SR_CGI, SR_ERROR, SR_NOMEM} SRCode;


void SetUpAccessLogging(char *accessLogName);

#endif
