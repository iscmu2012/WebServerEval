/*
    userver -- (pronounced you-server or micro-server).
    This file is part of the userver, a high-performance
    web server designed for performance experiments.

    Copyright (C) 2005-2011 Tim Brecht
    Based on the file originally Copyright (C) 2004  
    Hewlett-Packard Company

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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#if defined(__linux__)
#include <sys/sendfile.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>

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


/* Some of the routines in this file are support routines
 * that may be called by the OS specific implementation
 */

/* ---------------------------------------------------------------------- */
int
do_sendfile(int sd, struct rep *rep)
{
  struct info *ip;
  int hdr_len = 0;              /* length of the http header */
  int file_bytes = 0;           /* number of bytes coming from the file */
  int file_fd = 0;              /* file fd */
  int nwritten = 0;

  ip = info_ptr(sd);
  assert(ip != NULL);
  assert(rep == &ip->rep);

  /* Calculate some things:
   *   hdr_len - number of bytes to send using write()
   *   file_bytes - number of file bytes to send using sendfile()
   *   file_fd - fd to pass to sendfile()
   *   rep->offset - offset to pass to sendfile()
   */
  switch (ip->req.type) {
    case REQ_STATIC:
      hdr_len = (rep->end - rep->cur);
      /* The number of bytes coming from the file is:
       * The total number of bytes initially determined which includes the 
       * header, minus the number of bytes that have been sent so far
       * minus the number of bytes we still have left in the header to send.
       */
      file_bytes = rep->total_len - rep->nwritten - hdr_len;
      file_fd = rep->fd;
      break;

    case REQ_FASTCGI:
      hdr_len = 0;
      file_bytes = rep->total_len - hdr_len;
      file_fd = dyn_bufpool->track->mmap_fd;
      PRINT_TIME(sd, &tnow, &tprev,
        "rep->cur = %p dyn_bufpool->track->adjusted_addr = %p",
        rep->cur, dyn_bufpool->track->adjusted_addr);
      if (rep->offset == 0) {
        rep->offset = rep->cur - (char *) dyn_bufpool->track->adjusted_addr;
      }
      break;

    default:
      printf("do_sendfile(): invalid ip->req.type %d\n", ip->req.type);
      assert(0);
      break;
  }

  PRINT_TIME(sd, &tnow, &tprev,
    "hdr_len = %d file_bytes = %d file_fd = %d offset = %d",
    hdr_len, file_bytes, file_fd, rep->offset);
  PRINT_TIME(sd, &tnow, &tprev, "use_cache = %d", rep->use_cache);

#if defined(__linux__)
  nwritten = sendfile_linux(ip, hdr_len, file_bytes, file_fd);
#elif defined(FreeBSD)
  nwritten = sendfile_freebsd(ip, hdr_len, file_bytes, file_fd);
#elif defined(HPUX)
  printf("Not tested/implemented\n");
  exit(1);
  nwritten = sendfile_hpux(ip, hdr_len, file_bytes, file_fd);
#else
#error sendfile not implemented on this operating system
#endif

  return nwritten;
} /* do_sendfile */


/*----------------------------------------------------------------------*/
void
process_sendfile_errors(int sd, int err)
{
  switch (err) {
    case EAGAIN:
      num_socket_sendfile_eagain++;
      PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: EAGAIN would block");
      break;

    case ECONNRESET:
      PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: ECONNRESET ");
      num_socket_sendfile_reset++;
      break;

#if defined(FreeBSD)
    case ENOTCONN:
      PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: ENOTCONN ");
      num_socket_sendfile_enotconn++;
      break;
#endif

    case EPIPE:
      PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: EPIPE ");
      num_socket_sendfile_epipe++;
      break;

    case EBUSY:
      PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: EBUSY ");
      num_socket_sendfile_ebusy++;
      break;

    default:
      num_socket_sendfile_failed_others++;
      PRINT_TIME(sd, &tnow, &tprev, "do_sendfile: other failure ");
      DEBG(MSG_WRITEV, "do_sendfile: sendfile failed err = %d\n", err);
      printf("do_sendfile: sendfile failed errno = %d\n", err);
      break;
  } /* switch */
}

/*----------------------------------------------------------------------*/
void
fcntl_fail(char *err)
{
  printf("Error: fcntl() failed: %s\n", err);
  exit(1);
}

/*----------------------------------------------------------------------*/
/* If blocking sendfile is turned on we need to make the socket
 * blocking. Returns oldflags.
 */
int
blocking_sendfile_on(int sd)
{
  int oldflags = 0;
  int newflags = 0;

  if (options.blocking_sendfile) {
    /* Turn off non-blocking for this socket */
    oldflags = fcntl(sd, F_GETFL, 0);
    if (oldflags < 0) {
      fcntl_fail("F_GETFL failed: Could not retrieve socket flags");
    }

    newflags = fcntl(sd, F_SETFL, oldflags & ~O_NONBLOCK);

    if (newflags < 0) {
      fcntl_fail("F_SETFL failed: Could not set socket to blocking");
    }
  }

  return oldflags;
} /* blocking_sendfile_on */

/*----------------------------------------------------------------------*/
/* If blocking sendfile is turned on we need to turn off blocking
 * on the socket because blocking was turned on previously
 */
void
blocking_sendfile_off(int sd, int oldflags)
{
  int newflags = 0;

  if (options.blocking_sendfile) {
    // Make the socket non-blocking again
    newflags = fcntl(sd, F_SETFL, oldflags);
    if (newflags < 0) {
      fcntl_fail("F_SETFL failed: Could not set socket non-blocking");
    }
  }
} /* blocking_sendfile_off */

/*----------------------------------------------------------------------*/


#endif /* HAVE_SENDFILE */
