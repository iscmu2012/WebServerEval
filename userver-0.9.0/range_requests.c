/*
    userver -- (pronounced you-server or micro-server).
    This file is part of the userver, a high-performance 
    web server designed for performance experiments.
          
    This file is Copyright (C) 2011  Tim Brecht

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


#include "reply_status.h"
#include "range_requests.h"
#include "debug.h"

// #define LOCAL_DEBUG
#include "local_debug.h"

/* ---------------------------------------------------------------------- */
/* Determine if a request is for a range of bytes or not */
int
range_request(struct info *ip)
{
  return (ip->req.range_request);
}

/* ---------------------------------------------------------------------- */
/* Determine the length (number of bytes) in a range request */
off_t
range_len(struct info * ip)
{
  ssize_t len = 0;
  ldbg("range_len: ip->req.range_start = %zd\n", ip->req.range_start);
  ldbg("range_len: ip->req.range_end = %zd\n", ip->req.range_end);


  assert(ip->req.range_start >= 0);
  assert(ip->req.range_end >= 0);
  assert(ip->req.range_end >= ip->req.range_start);
  assert(ip->req.range_request);

  len = ip->req.range_end - ip->req.range_start + 1;

  ldbg("len: returning %zd\n", len);
  return (len);
}

/* ---------------------------------------------------------------------- */
/* version   - http version e.g., 0 or 1
 * filesize  - total size of the file
 * type      - content type string
 * returns the length of the generated header
 */

int
do_range_header(struct info *ip, int version, size_t filesize, char *type)
{
  int hdr_len = 0;
  struct rep *rep = 0;
  struct req *req = 0;
  int range_len = 0;

  assert(ip);
  rep = &(ip->rep);
  req = &(ip->req);
  assert(rep);
  assert(req);

  assert(req->range_request);

  range_len = req->range_end - req->range_start + 1;
  ldbg("range_len = %d\n", range_len);

  if (type) {
    hdr_len = snprintf(rep->cur, rep->buf_size,
      HTTP_PARTIAL_CONTENT_STR_ID_LEN_TYPE, version,
      server_ident, (unsigned long int) range_len, type,
      req->range_start, req->range_end, (long long int) filesize);
  } else {
    hdr_len = snprintf(rep->cur, rep->buf_size,
      HTTP_PARTIAL_CONTENT_STR_ID_LEN, version,
      server_ident, (unsigned long int) range_len,
      req->range_start, req->range_end, (long long int) filesize);
  }

  return (hdr_len);

} /* do_range_header */

/* ---------------------------------------------------------------------- */
