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
#ifdef FreeBSD

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

/* ---------------------------------------------------------------------- */
int
sendfile_freebsd(struct info *ip, int hdr_len, int file_bytes, int file_fd)
{
  struct rep *rep = 0;
  struct iovec hdrtrl[2];
  struct sf_hdtr hdtr;
  struct iovec *hdr_iov = 0;
  int remainder = 0;
  off_t nwritten = 0;
  off_t towrite = 0;
  int flags = 0;
  int sd = -1;
  int trace_fd = -1;
  int rc = 0;
  int save_errno = 0;
  int oldflags = 0;

  assert(ip != NULL);
  rep = &ip->rep;
  sd = ip->sd;

  PRINT_TIME(sd, &tnow, &tprev, "FreeBSD");
  PRINT_TIME(sd, &tnow, &tprev, "rep->cur = %p rep->end = %p "
    "diff = %d", rep->cur, rep->end, rep->end - rep->cur);

  PRINT_TIME(sd, &tnow, &tprev, "hdr_len = %d file_bytes = %d",
    hdr_len, file_bytes);

  /* Set up the header which is sent before the file */
  if (hdr_len > 0) {
    /* on FreeBSD sendfile uses header and trailer pointers arrays */
    hdrtrl[0].iov_base = rep->cur;
    hdrtrl[0].iov_len = rep->end - rep->cur;
  } else {
    hdrtrl[0].iov_base = 0;
    hdrtrl[0].iov_len = 0;
  }

  /* This is the trailer which is sent after the file */
  /* We currently never have anything to send here */

  hdrtrl[1].iov_base = 0;
  hdrtrl[1].iov_len = 0;

  hdtr.headers = &hdrtrl[0];
  hdtr.hdr_cnt = 1;
  hdtr.trailers = &hdrtrl[1];
  hdtr.trl_cnt = 1;

  PRINT_TIME(sd, &tnow, &tprev, "hdrtrl[0].iov_base = %p "
    "hdrtrl[0].iov_len = %d", hdrtrl[0].iov_base, hdrtrl[0].iov_len);

#ifdef DEBUG_OUTPUT_BUFS
  print_buf("sendfile_freebsd (header)",
    sd, hdtr.headers[0].iov_base, hdtr.headers[0].iov_len);
  print_sendfile_buf("sendfile_freebsd (body)",
    sd, rep->fd, rep->offset, file_bytes);
#endif

  PRINT_TIME(sd, &tnow, &tprev, "offset = %d", rep->offset);
  num_socket_sendfile_calls++;

  flags = 0;

  /* This code hasn't been checked/tested since a code reorganization
   * and support for range requests has been added
   */
  if (options.cache_miss_skip) {
    /* in this case skip_info counts how many times this request
     * has been skipped
     */
    if (ip->skip_info > options.cache_miss_skip) {
      PRINT_TIME(sd, &tnow, &tprev,
        "ip->skip_info = %d clearing", ip->skip_info);
      flags = 0;
      ip->skip_info = 0;
    } else {

#if defined (DARWIN)
      printf("MAC OS X doesn't seem to have SF_NODISKIO\n");
      exit(1);
#else
      /* Tell sendfile not to block on disk I/0 */
      flags = SF_NODISKIO;
      /* we'll see if it gets skipped when sendfile returns */
#endif
    }
  }

  /* On FreeBSD and Mac OS X we only specify the number of 
   * bytes from the file. The header specifies it's own length.
   */
  towrite = file_bytes;

  PRINT_TIME(sd, &tnow, &tprev, "flags = %d towrite = %zu", flags, towrite);

  oldflags = blocking_sendfile_on(sd);

  TRACE(EVT_SENDFILE, trace_fd = sd;
#if defined (DARWIN)
    PRINT_TIME(sd, &tnow, &tprev, "DARWIN");
    /* On MAC OS X we indicated the number of bytes to write from the file 
     * in * towrite and after the call towrite will contain the number
     * of bytes written.
     */
    rc = sendfile(rep->fd, sd, rep->offset,
      &towrite, &hdtr, flags); nwritten = towrite;
#else
    /* On FreeBSD sendfile returns 0 on success -1 on failure
     * the total number of bytes written is returned in nwritten.
     * towrite is the number of files from the file.
     */
    rc = sendfile(rep->fd, sd, rep->offset, towrite, &hdtr, &nwritten, flags);
#endif
#if defined (TRACE_ON)
    tmp_extra1 = nwritten;
#endif
    save_errno = errno;);

  blocking_sendfile_off(sd, oldflags);

  PRINT_TIME(sd, &tnow, &tprev, "rc = %d nwritten = %d save_errno = %d",
    rc, nwritten, save_errno);

  if (rc < 0) {
    switch (errno) {
      case EBUSY:
        /* Want to track how many EBUSYs we get */
        process_sendfile_errors(sd, save_errno);
        assert(options.cache_miss_skip);
        /* sendfile didn't send the request because it requires disk I/O */
        ip->skip_info++;
        num_cache_skips++;
        break;

      case EAGAIN:
        /* Do nothing, on BSD EAGAIN indicates EWOULDBLOCK */
        /* But it's probably sent some bytes (e.g., a sockbuf full */
        break;

      default:
        process_sendfile_errors(sd, save_errno);
        info_close_rep_fd(ip);
        PRINT_TIME(sd, &tnow, &tprev, "write failed -  returning -1");
        return -1;
        break;
    }
  }

  assert(nwritten >= 0);

  /* if nothing got written no need to update the things below. */
  if (nwritten == 0) {
    assert(save_errno == EAGAIN || save_errno == EBUSY);
    PRINT_TIME(sd, &tnow, &tprev,
      "sendfile nwritten = %d rc = %d save_errno = %d",
      nwritten, rc, save_errno);
    return 0;
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

  PRINT_TIME(sd, &tnow, &tprev, "rep->offset = %d bytes remaining = %d",
    rep->offset, rep->total_len - rep->nwritten);

  return nwritten;
}

/* ---------------------------------------------------------------------- */

#endif /* FreeBSD */
#endif /* HAVE_SENDFILE */
