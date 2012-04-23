/*
    userver -- (pronounced you-server or micro-server).
    This file is part of the userver, a high-performance 
    web server designed for performance experiments.
          
    This file is Copyright (C) 2004-2011  Tim Brecht

    Authors: Tim Brecht <brecht@cs.uwaterloo.ca>
    See AUTHORS file for list of contributors to the project.
  
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.
  
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307 USA
*/

#ifdef HAVE_SENDFILE
#ifdef HP_UX

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#include "common.h"
#include "debug.h"
#include "trace.h"
#include "stats.h"
#include "syscalls.h"
#include "state.h"
#include "options.h"
#include "do_sendfile_new.h"
#include "range_requests.h"

// #define LOCAL_DEBUG
#include "local_debug.h"


#define SEND_REMAINDER (0)

/* ---------------------------------------------------------------------- */
int
sendfile_hpux(struct info *ip, int hdr_len, int file_len, int file_fd)
{
  struct rep *rep = 0;
  struct iovec hdrtrl[2];
  struct iovec *hdr_iov = 0;
  int remainder = 0;
  int nwritten = 0;
  int sd = -1;
  off_t towrite = 0;
  int oldflags = 0;

  assert(ip != NULL);
  rep = &ip->rep;
  sd = ip->sd;

  PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: rep->cur = %p rep->end = %p "
    "diff = %d", rep->cur, rep->end, rep->end - rep->cur);

  if (range_request(ip)) {
    printf("Range request support has not been tested on HPUX\n");
    exit(1);
  }

  /* Set up the header which is sent before the file */
  if (hdr_len > 0) {
    /* on HP-UX sendfile includes iovs for a header and trailer */
    /* if they are not null they are sent before and after the file */
    hdrtrl[0].iov_base = rep->cur;
    hdrtrl[0].iov_len = rep->end - rep->cur;
  } else {
    hdrtrl[0].iov_base = 0;
    hdrtrl[0].iov_len = 0;
  }

  /* This is the trailer which is sent after the file */
  hdrtrl[1].iov_base = 0;
  hdrtrl[1].iov_len = 0;

  PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: hdrtrl[0].iov_base = %p "
    "len = %d", hdrtrl[0].iov_base, hdrtrl[0].iov_len);

  oldflags = blocking_sendfile_on(sd);

  PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: offset = %d", rep->offset);
  num_socket_sendfile_calls++;

  towrite = file_bytes;

  TRACE(EVT_SENDFILE,
    trace_fd = sd;
    rc = sendfile(sd, rep->fd, rep->offset, file_bytes, hdrtrl, 0);
    save_errno = errno;);

  nwritten = rc;
  PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: nwritten = %d", nwritten);

  blocking_sendfile_off(sd, oldflags);

  if (nwritten <= 0) {
    process_sendfile_errors(sd, save_errno);
    if (save_errno == EAGAIN) {
      PRINT_TIME(sd, &tnow, &tprev,
        "do_sendfile: EAGAIN would block -  returning 0");
      return 0;
    }

    info_close_rep_fd(ip);
    PRINT_TIME(sd, &tnow, &tprev,
      "do_sendfile: write failed -  returning -1");
    return -1;
  }

  num_socket_sendfile_successful++;
  num_socket_sendfile_bytes += (double) nwritten;
  num_socket_write_bytes_total += (double) nwritten;


  hdr_iov = &hdrtrl[0];

  /* some bytes have been written so we need to update the iovecs */
  if (hdr_iov->iov_len != 0) {
    if (nwritten <= hdr_iov->iov_len) {
      rep->cur += nwritten;
    } else {
      remainder = nwritten - hdr_iov->iov_len;
      /* some of these were the last of the header bytes */
      rep->cur += hdr_iov->iov_len;
      hdr_iov->iov_len = 0;
      /* the remainder were sent from the file */
      rep->offset += remainder;
    }
    if (rep->cur == rep->end) {
      rep->end = rep->cur = rep->buf;
    }
  } else {
    rep->offset += nwritten;
  }

  rep->nwritten += nwritten;

  return nwritten;
}

/* ---------------------------------------------------------------------- */

#endif /* HP_UX */
#endif /* HAVE_SENDFILE */
