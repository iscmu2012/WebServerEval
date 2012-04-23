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

/* ---------------------------------------------------------------------- */

#ifdef HAVE_SENDFILE
#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/sendfile.h>

#include "do_sendfile_new.h"
#include "common.h"
#include "debug.h"
#include "trace.h"
#include "stats.h"
#include "syscalls.h"
#include "state.h"
#include "options.h"
#include "range_requests.h"

// #define LOCAL_DEBUG
#include "local_debug.h"

extern void process_write_errors(int sd, int err);

/* ---------------------------------------------------------------------- */
int
sendfile_linux(struct info *ip, int hdr_len, int file_bytes, int file_fd)
{
  struct rep *rep = 0;
  int trace_fd = 0;
  int rc = 0;
  int save_errno = 0;
  int sd = -1;
  int nwritten = 0;
  int towrite = 0;
  off_t tmp_offset = 0;
  int value = 0;                /* used for TCP_CORK */
  int oldflags = 0;

  assert(ip != NULL);
  rep = &ip->rep;
  sd = ip->sd;

  /*
   * If there is static header data in the buffer, send that first using
   * write().  Later, we'll send the main content using sendfile().
   */
  if (hdr_len > 0) {
    if (options.skip_header) {
      rep->cur = rep->end;
      hdr_len = 0;
    } else {
      PRINT_TIME(sd, &tnow, &tprev, "write buffer data first");
      PRINT_TIME(sd, &tnow, &tprev, "rep->cur = %p rep->end = %p diff = %d",
        rep->cur, rep->end, (int) (rep->end - rep->cur));

      /* Only do the TCP_CORK if the file has something in it */
      if ((options.use_tcp_cork && !rep->is_corked) && file_bytes > 0) {
        value = 1;
        TRACE(EVT_TCP_CORK,
          trace_fd = sd;
          rc = setsockopt(sd, SOL_TCP, TCP_CORK, &value, sizeof(value)););
        if (rc < 0) {
          printf("unable to TCP_CORK\n");
          exit(-1);
        }
        rep->is_corked = 1;
        num_socket_corked++;
      }

      num_socket_write_calls++;
      towrite = rep->end - rep->cur;

#ifdef DEBUG_OUTPUT_BUFS
      print_buf("do_sendfile (header)", sd, rep->cur, (rep->end - rep->cur));
#endif

      /* Write the header */
      TRACE(EVT_WRITE_SOCK,
        trace_fd = sd;
        rc = write(sd, rep->cur, rep->end - rep->cur); save_errno = errno;);

      nwritten = rc;
      PRINT_TIME(sd, &tnow, &tprev, "write returns nwritten = %d "
        "errno = %d", nwritten, save_errno);

      PRINT_TIME(sd, &tnow, &tprev,
        "towrite = %d nwritten = %d", towrite, nwritten);
#ifdef HAVE_EPOLL
      if (options.epoll_edge_triggered && nwritten >= 0) {
        if (nwritten < towrite) {
          sd_state_not_writable(info_ptr(sd));
        }
      }
#endif /* HAVE_EPOLL */

      if (nwritten <= 0) {
        process_write_errors(sd, save_errno);
        if (save_errno == EAGAIN) {
          PRINT_TIME(sd, &tnow, &tprev, "EAGAIN would block -  returning 0");
#ifdef HAVE_EPOLL
          if (options.epoll_edge_triggered) {
            sd_state_not_writable(info_ptr(sd));
          }
#endif /* HAVE_EPOLL */
          return 0;
        }

        info_close_rep_fd(ip);
        PRINT_TIME(sd, &tnow, &tprev, "write failed -  returning -1");
        return -1;
      } else {
        num_socket_write_successful++;
        num_socket_write_bytes += (double) nwritten;
        num_socket_write_bytes_total += (double) nwritten;
      } /* else */

      /* set_fsm_state(ip, FSM_WRITING_REPLY); */
      rep->cur += nwritten;
      if (rep->cur == rep->end) {
        rep->end = rep->cur = rep->buf;
      }
      rep->nwritten += nwritten;
      hdr_len -= nwritten;
    } /* else */
  }

  /* if */
  /*
   * Assuming there is no more static header data to send, go ahead and
   * send the main content using sendfile().
   */
  if (hdr_len == 0 && file_bytes > 0) {

    PRINT_TIME(sd, &tnow, &tprev, "calling sendfile file_fd = %d", file_fd);
    num_socket_sendfile_calls++;
    PRINT_TIME(sd, &tnow, &tprev, "offset = %d", rep->offset);

    towrite = file_bytes;
    PRINT_TIME(sd, &tnow, &tprev,
      "rep->offset = %d bytes remaining = %d file_bytes = %d",
      rep->offset, rep->total_len - rep->nwritten, file_bytes);

#ifdef DEBUG_OUTPUT_BUFS
    print_sendfile_buf("do_sendfile (body)", sd, file_fd,
      rep->offset, rep->total_len - rep->nwritten);
#endif

    oldflags = blocking_sendfile_on(sd);

    tmp_offset = (off_t) rep->offset;
    TRACE(EVT_SENDFILE, trace_fd = sd;
      /* Note that the kernel updates the offset here */
      /* NOTE: This causes problems with TCP_NODELAY */
      /* Interestingly the kernel thinks that there are more
       * bytes that are to be written so it delays sending
       * a partial packet even if TCP_NODELAY is set (i.e., Nagle's
       * algorithm is disabled
       * rc = sendfile(sd, file_fd, (off_t *) &(rep->offset), ~0UL);
       */
      /*
         rc = sendfile(sd, file_fd, (off_t *) &(rep->offset), 
         rep->total_len - rep->nwritten);
       */
      rc = sendfile(sd, file_fd, &tmp_offset, towrite);
#if defined(TRACE_ON)
      tmp_extra1 = (int) tmp_offset - rep->offset;
#endif
      );
    save_errno = errno;

    rep->offset = (int) tmp_offset;

    nwritten = rc;
    PRINT_TIME(sd, &tnow, &tprev, "nwritten = %d", nwritten);
    PRINT_TIME(sd, &tnow, &tprev,
      "towrite = %d nwritten = %d", towrite, nwritten);
#ifdef HAVE_EPOLL
    if (options.epoll_edge_triggered && nwritten >= 0) {
      if (nwritten < towrite) {
        sd_state_not_writable(info_ptr(sd));
      }
    }
#endif /* HAVE_EPOLL */

    blocking_sendfile_off(sd, oldflags);

    if (nwritten == 0) {
      printf("sendfile was to send %d bytes but sent 0\n", towrite);
      printf("Suspected kernel bug\n");
      assert(nwritten != 0);
      exit(1);
      num_socket_sendfile_zero++;
    } else if (nwritten < 0) {
      process_sendfile_errors(sd, save_errno);
      if (save_errno == EAGAIN) {
        PRINT_TIME(sd, &tnow, &tprev,
          "do_sendfile: EAGAIN would block -  returning 0");
#ifdef HAVE_EPOLL
        if (options.epoll_edge_triggered) {
          sd_state_not_writable(info_ptr(sd));
        }
#endif /* HAVE_EPOLL */
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

    if (options.use_tcp_cork && rep->is_corked) {
      /* Uncork the TCP queue now that the header has been sent */
      value = 0;
      TRACE(EVT_TCP_CORK,
        trace_fd = sd;
        rc = setsockopt(sd, SOL_TCP, TCP_CORK, &value, sizeof(value)););
      if (rc < 0) {
        printf("do_sendfile: unable to TCP_CORK\n");
        exit(-1);
      }
      rep->is_corked = 0;
      num_socket_uncorked++;
    }

    /* set_fsm_state(ip, FSM_WRITING_REPLY); */
    rep->nwritten += nwritten;
  }
  return nwritten;
}

/* ---------------------------------------------------------------------- */

#endif /* __linux__ */
#endif /* HAVE_SENDFILE */
