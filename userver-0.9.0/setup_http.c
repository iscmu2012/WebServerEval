/*
    userver -- (pronounced you-server or micro-server).
    This file is part of the userver, a high-performance 
    web server designed for performance experiments.

    Copyright (C) 2011 Tim Brecht

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

/*----------------------------------------------------------------------*/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <errno.h>
#include <stdio.h>

#include "debug.h"
#include "options.h"
#include "stats.h"
#include "reply_status.h"
#include "trace.h"
#include "cntl_intr.h"
#include "range_requests.h"


static int work_around_set_value(cacheinfo * cache);
static int setup_static(struct info *ip);
static int setup_get_head_post(struct info *ip);
static int setup_fastcgi(struct info *ip);
static int setup_static_cached(struct info *ip);
static int setup_static_not_cached(struct info *ip);
static int get_version(struct req *req);
int build_header(struct info *ip, int filesize);

// #define LOCAL_DEBUG
#include "local_debug.h"

void print_rep_info(struct rep *rep);

#ifdef HAVE_ENCRYPTION
int encrypt_buf(const char *inbuf, char *outbuf, int len, int withhdr);
#endif /* HAVE_ENCRYPTION */

#define DEFAULT_NAME           "index.html"

/*----------------------------------------------------------------------*/
int special_uri(char *uri, int sd);
void sleep_usecs(int usecs);
char *http_content_type(char *uri);

/*----------------------------------------------------------------------*/

int
setup_http(struct info *ip)
{
  int retval = SETUP_HTTP_CONTINUE;

  struct rep *rep;
  struct req *req;
  int sd = ip->sd;
  int rc = -1;
  int version = 0;
  int uri_len = 0;

#ifdef HAVE_ENCRYPTION
  int n, encrypted;
#endif /* HAVE_ENCRYPTION */

  assert(ip != NULL);
  rep = &ip->rep;
  req = &ip->req;
  assert(rep);
  assert(req);

  if (range_request(ip) && options.skip_header) {
    printf("Can't skip header when doing range requests\n");
    exit(1);
  }

  version = get_version(req);

#ifdef UMEMREDUCTION
  // TODO:
  // What is this doing? I don't think this can work
  // For some strange reason it compiles.
  char uri[req->uri_len + options.doc_root_len];
  uri_len = req->uri_len;
#else
  uri_len = strlen(req->uri);
#endif /* UMEMREDUCTION */

  PRINT_TIME(sd, &tnow, &tprev,
    "entered uri = %s uri_len = %d", req->uri, uri_len);
  set_fsm_state(ip, FSM_WRITING_REPLY);

  /* check to see if we should add index.html */
#ifdef UMEMREDUCTION
  // TODO
  // This will need to change if we support doc_root length
  if (options.doc_root_len != 0) {
    printf("--doc-root can not be used with UMEMREDUCTION yet\n");
    exit(1);
  }

  if (req->uri[uri_len - 1] == '/') {
#else
  if (options.doc_root_len == uri_len || req->uri[uri_len - 1] == '/') {
#endif /* UMEMREDUCTION */
    strcat(req->uri, DEFAULT_NAME);
    PRINT_TIME(sd, &tnow, &tprev, "rewrote uri = [%s]", req->uri);
  }

  /* By default we don't skip anything */
  ip->skip = 0;
  ip->skip_info = 0;

  if (options.victim_str[0] != '\0') {
    /* if the specified victim_str matches any 
     * portion of the uri it is a vicitim 
     */
    if (strstr(req->uri, options.victim_str) != 0) {
      if (options.victim_skip == 0) {
        printf("A victim for skipping was specified but ");
        printf("the --victim-skip value is 0\n");
        exit(1);
      }
      ip->skip = options.victim_skip;
      ldbg("Victim string is %s and skip count is = %d\n",
        options.victim_str, ip->skip);
    }
  }
#ifdef UMEMREDUCTION
  if (options.doc_root_len != 0) {
    printf("--doc-root can not be used with UMEMREDUCTION yet\n");
    exit(1);
  }

  /* special quick check to see if request is an escape */
  if (req->uri[0] == 'e') {
    /* returns 0 if not a special uri */
    if ((rc = special_uri(req->uri, sd)) > 0) {
#else
  /* special quick check to see if request is an escape */
  if (req->uri[options.doc_root_len] == 'e') {
    /* returns 0 if not a special uri */
    if ((rc = special_uri(req->uri + options.doc_root_len, sd)) > 0) {
#endif /* UMEMREDUCTION */
      notification_on();
      PRINT_TIME(sd, &tnow, &tprev, "returning after special_uri");
      return 1;
    }
  }

  switch (req->method) {
    case HTTP_GET:
    case HTTP_HEAD:
    case HTTP_POST:
      retval = setup_get_head_post(ip);
      PRINT_TIME(sd, &tnow, &tprev, "done setup_get_head_post");
      break;

    case HTTP_NOT_IMPL:
      reply_status_fill(ip, HTTP_NOT_IMPLEMENTED, req->uri, version);
      /* Close the connection after sending this response. */
      req->close = 1;
      break;

    case HTTP_RETURN_ERROR:
      /*
       * We should have already called reply_status_fill() to write the
       * HTTP response.
       * Close the connection after sending this response.
       */
      req->close = 1;
      break;

    default:
      assert(0);
      break;
  } /* switch (req->method) */

  PRINT_TIME(sd, &tnow, &tprev, "rep.total_len = %d returning %d",
    rep->total_len, retval);

  return retval;

} /* setup_http */



/*----------------------------------------------------------------------*/
static int
setup_get_head_post(struct info *ip)
{
  int sd = -1;
  struct rep *rep = 0;
  struct req *req = 0;
  rep = &(ip->rep);
  int retval = SETUP_HTTP_CONTINUE;

  rep = &ip->rep;
  req = &ip->req;
  sd = ip->sd;

  //TODO: HEADs probably shouldn't be counted in num_good_gets
  num_good_gets++;

  /* reset these for the new request
   * Offset may get adjusted later for range requests
   * Also note that offset is a call by reference value that gets changed
   * by calls to sendfile 
   */
  rep->nwritten = 0;
  rep->offset = 0;
  PRINT_TIME(sd, &tnow, &tprev, "offset = %d nwritten = %d",
    rep->offset, rep->nwritten);

  /* at this point we have the full request */
  if (options.delay && options.state_delay == OPT_NO_STATE_DELAY) {
    PRINT_DELAY(sd, &tnow, &tprev, "before delay sleep_usecs");
    sleep_usecs(options.delay);
    PRINT_DELAY(sd, &tnow, &tprev, "after delay sleep_usecs");
  }
  /* if */
  switch (req->type) {
    case REQ_STATIC:
      retval = setup_static(ip);
      break;

    case REQ_FASTCGI:
      retval = setup_fastcgi(ip);
      break;

    default:
      assert(0);
      break;
  } /* switch (req->type) */

  if (retval != SETUP_HTTP_CONTINUE) {
    PRINT_TIME(sd, &tnow, &tprev, "returning retval = %d", retval);
    return retval;
  }

  /*
   * By now, any HTTP headers should have been written into req->buf,
   * and the following variables should be accurate:
   *   rep->use_cache, req->total_len, req->cur, req->end
   * and unless --skip-header is used
   *   hdr_len
   */

  /* for HEAD requests, omit the content */
  if (req->method == HTTP_HEAD) {
    info_close_rep_fd(ip);
    rep->total_len = 0;
    if (options.skip_header) {
      rep->end = rep->cur;
    } else {
      // rep->end = rep->cur + hdr_len;
    }
  }
#if 0
  /* TODO: this probably isn't a good place to do this. Instead do this
   * in the code where the header and length, etc. are known 
   */
  if (!options.skip_header) {
    /*
     * Up to this point, rep->total_len meant the number of bytes in the
     * file or in the dynamic response (e.g. FastCGI).  Now, we amend it to
     * also include our prepended HTTP reply headers.  Thus, it is the
     * total number of bytes that we will send to the client.
     */
    rep->total_len += hdr_len;
  }
#endif

  DEBG(MSG_READABLE,
    "rep->buf = %p rep->cur = %p rep->end = %p retval = %d\n", rep->buf,
    rep->cur, rep->end, retval);

  return retval;
} /* setup_get_head_post */


/*----------------------------------------------------------------------*/
/* When we are done:
 * rep->fd         is the fd being used (if there is one)
 * rep->buf        points at the start of the buffer being used
 * rep->cur        points at the location of the first byte to send
 * rep->end        points at the location of the last  byte to send
 * rep->total_len  is the total number of bytes to send including the header
 * rep->cache      points to the cache info field if one is being used
 * rep->use_cache  is 1 if we are using the cached of open fds
 */

static int
setup_static(struct info *ip)
{
  struct req *req = 0;
  struct rep *rep = 0;
  int sd = -1;
  int trace_fd = -1;
  int version;
  int rc = 0;
  cacheinfo *cache = NULL;
  int retval = SETUP_HTTP_CONTINUE;

  req = &(ip->req);
  rep = &(ip->rep);
  sd = ip->sd;

  version = get_version(req);
  PRINT_TIME(sd, &tnow, &tprev, "setup_static entered");

  if (options.caching_on && req->type == REQ_STATIC) {
    PRINT_TIME(sd, &tnow, &tprev, "calling cache_add");
    TRACE(EVT_CACHE_ADD, trace_fd = sd;
      cache = cache_add(req->uri, version); rc = work_around_set_value(cache);
      /* cache ? rc = (int) (cache->header_len + cache->buf_len) : rc = 0; */
      );
  }

  if (cache) {
    rep->cache = cache;
    rep->use_cache = 1;
    rep->fd = cache->fd;
    assert(cache->ref_count >= 1);
    retval = setup_static_cached(ip);
  } else {
    rep->cache = NULL;
    rep->use_cache = 0;
    retval = setup_static_not_cached(ip);
  } /* else */

  PRINT_TIME(sd, &tnow, &tprev, "buf = %p cur = %p end = %p end-cur = %d",
    rep->buf, rep->cur, rep->end, rep->end - rep->cur);

  PRINT_TIME(sd, &tnow, &tprev, "total_len = %d offset %d retval = %d",
    rep->total_len, rep->offset, retval);

  return retval;
} /* setup_static */


/*----------------------------------------------------------------------*/
static int
setup_static_cached(struct info *ip)
{
  struct req *req = 0;
  struct rep *rep = 0;
  int sd = -1;
  int rc = 0;
  int version = 0;
  cacheinfo *cache = NULL;
  int hdr_len = 0;

  req = &ip->req;
  rep = &ip->rep;
  sd = ip->sd;

  get_version(req);

  /* This is handling the case where there is a cache hit so this must be valid */
  cache = rep->cache;
  assert(cache);
  assert(rep->use_cache);


  rep->end = rep->buf;
  rep->cur = rep->buf;

  PRINT_TIME(sd, &tnow, &tprev, "is in cache");

  /* Handle if-modified-since requests */
  if (req->ifmodsince > 0) {
    num_reqs_if_modified_since++;
    rc = check_if_modified_since(cache, req->ifmodsince);
    if (rc == 0) {
      /* Just send the 304 header */
      reply_status_fill(ip, HTTP_NOT_MODIFIED, req->uri, version);
      num_reply_http_not_modified++;
      return SETUP_HTTP_CONTINUE;
    }
  }


  /* Caching is turned on */
  /* TODO: Move this later to fit with bits of code below */
  if (range_request(ip)) {
    assert(!options.skip_header);

    rep->http_status = HTTP_PARTIAL_CONTENT;

    /* We can't use the cached header so we need to generate one.
     * The header will come out of the reply buffer and the body
     * from the mmapped cache 
     */

    rep->buf = rep->static_buf;
    rep->end = rep->buf;
    rep->cur = rep->buf;

    /* To build the header it needs to know the file size cache->buf_len */
    hdr_len = build_header(ip, cache->buf_len);

    /* This is for the header */
    rep->iovec[0].iov_base = rep->buf;
    rep->iovec[0].iov_len = hdr_len;
    /* This is for the body */
    rep->iovec[1].iov_base = cache->buf + req->range_start;
    rep->iovec[1].iov_len = MIN(cache->buf_len, range_len(ip));

    /* If we are using sendfile set the location of the first byte */
    rep->offset = req->range_start;
    rep->total_len = hdr_len + MIN(cache->buf_len, range_len(ip));

    /* These aren't used ? when writing from cache */
    /*
       rep->cur = 
     */
    rep->end = rep->buf + hdr_len;

  } else {
    /* header is already available in the cache */
    rep->http_status = HTTP_OK;

    rep->buf = rep->cache->header;
    rep->end = rep->buf;
    rep->cur = rep->buf;

    if (options.skip_header) {
      rep->iovec[0].iov_base = 0;
      rep->iovec[0].iov_len = 0;
      rep->iovec[1].iov_base = cache->buf;
      rep->iovec[1].iov_len = cache->buf_len;
      rep->offset = 0;
      rep->total_len = cache->buf_len;
      // hdr_len = 0;
      // rep->end += hdr_len;
      // PRINT_TIME(sd, &tnow, &tprev, "hdr_len = %d total_len = %d end = %p", hdr_len, rep->total_len, rep->end);
    } else {
      rep->iovec[0].iov_base = cache->header;
      rep->iovec[0].iov_len = cache->header_len;
      rep->iovec[1].iov_base = cache->buf;
      rep->iovec[1].iov_len = cache->buf_len;
      rep->offset = 0;
      rep->total_len = cache->header_len + cache->buf_len;
      rep->end = rep->buf + cache->header_len;
    }
  }

  print_rep_info(rep);

#ifdef HAVE_ENCRYPTION
  encrypted = 0;

  /* If encryption is compiled in, then determine if the option is
   * enabled and whether or not this file needs to be encrypted.
   * If it's to be encrypted then encrypt it into the encryption
   * buffer and the change iovec pointer in the reply to point to
   * the encryption buffer.
   */
  if (options.encrypt_data) {
    num_encrypt_tot_reqs++;
    if ((options.encrypt_max_file_size == 0UL) ||
      ((unsigned long) cache->buf_len <= options.encrypt_max_file_size)) {
      num_encrypt_size_ok++;
      if (options.encrypt_percent < 100) {
        n = (rand() % 100) + 1;
        if (n <= options.encrypt_percent)
          encrypted = 1;
      } else {
        encrypted = 1;
      }
      if (encrypted) {
        num_encrypt_encrypted++;
        rep->fd = cache->encryptfd;
        n = encrypt_buf(cache->buf, cache->encryptbuf, cache->buf_len,
          options.skip_header);
        assert(n >= 0);
        num_encrypt_bytes += (long long) n;
        rep->iovec[1].iov_base = cache->encryptbuf;
      }
    }
  }
#endif /* HAVE_ENCRYPTION */

#ifdef HAVE_AIO_LAYER
#ifdef HAVE_ENCRYPTION
  if (encrypted) {
    cache->encrypt_buf_handle;
  } else {
    cache->file_reg_handle;
  }
#else
  rep->write_reg_handle = cache->file_reg_handle;
#endif /* HAVE_ENCRYPTION */
  PRINT_TIME(sd, &tnow, &tprev, "write_reg_handle = %p",
    rep->write_reg_handle);
#endif /* HAVE_AIO_LAYER */

  return SETUP_HTTP_CONTINUE;
} /* setup_static_cached */

/*----------------------------------------------------------------------*/
static int
setup_static_not_cached(struct info *ip)
{
  struct stat stat;
  struct rep *rep = 0;
  struct req *req = 0;
  int sd = -1;
  int trace_fd = -1;
  int rc = 0;
  int err = 0;
  int version = 0;
  int hdr_len = 0;
  int bytes_to_send = 0;

  rep = &ip->rep;
  req = &ip->req;
  sd = ip->sd;

  version = get_version(req);

  /* Not using caching */
  PRINT_TIME(sd, &tnow, &tprev, "is not in cache or not using cache");

  assert(rep->use_cache == 0);
  assert(rep->cache == NULL);

#ifdef UMEMREDUCTION
  strcpy(uri, options.doc_root);
  strcat(uri, req->uri);
#endif /* UMEMREDUCTION */

  TRACE(EVT_OPEN_FILE, trace_fd = sd;
#ifdef UMEMREDUCTION
    rc = open(uri, O_RDONLY);
#else
    rc = open(req->uri, O_RDONLY);
#endif /* UMEMREDUCTION */
    err = errno;);

  PRINT_TIME(sd, &tnow, &tprev, "setup_static_not_cached: open returns %d",
    rc);
  rep->fd = rc;

  if (rep->fd < 0) {
    switch (err) {
        // case EACCES:
      case ENOENT:
        PRINT_TIME(sd, &tnow, &tprev,
          "setup_static_not_cached: "
          "open failed - returning after calling do_close");
        PRINT_TIME(sd, &tnow, &tprev, "setup_http: " "errno = %d", err);
        num_failed_open++;
        /* perror (req->uri); */
        reply_status_fill(ip, HTTP_NOT_FOUND, req->uri, version);
        notification_on();
        return SETUP_HTTP_CONTINUE;
        break;

      default:
        printf("setup_static_not_cached: open failed - unhandled reason\n");
        printf("shutting down\n");
        printf("setup_static_not_cached:  errno = %d\n", err);
        errno = err;
        perror(req->uri);
        exit(1);
        break;
    }
  }

  TRACE(EVT_FSTAT, trace_fd = rep->fd; rc = fstat(rep->fd, &stat););

  if (rc < 0) {
    num_failed_stat++;
    /* perror (req->uri); */
    info_close_rep_fd(ip);
    notification_on();
    PRINT_TIME(sd, &tnow, &tprev,
      "fstat failed - returning after calling do_close");
    return do_close(ip, REASON_FSTAT_FAILED);
  }

  /* Check that the request is for a file and not for a directory */
  if (S_ISDIR(stat.st_mode)) {
    PRINT_TIME(sd, &tnow, &tprev, "file stat failed");

#ifdef HAVE_DIRECTORY_LISTING
    directory_list(ip);
#else
    reply_status_fill(ip, HTTP_FORBIDDEN, req->uri, version);
    return SETUP_HTTP_CONTINUE;
#endif /* HAVE_DIRECTORY_LISTING */
  } else {
    rep->total_len = stat.st_size;
  }

  /* Handle if-modified-since requests */
  if (req->ifmodsince > 0) {
    num_reqs_if_modified_since++;
    num_extra_fstat++;
    if (stat.st_mtime <= req->ifmodsince) {
      /* Hasn't been modified since, so send 304 hdr */
      reply_status_fill(ip, HTTP_NOT_MODIFIED, req->uri, version);
      num_reply_http_not_modified++;
      return SETUP_HTTP_CONTINUE;
    }
  }

  rep->end = rep->buf;
  rep->cur = rep->buf;

  /* Not servicing out of the cache */

  if (!options.skip_header) {
    hdr_len = build_header(ip, stat.st_size);
  } else {
    hdr_len = 0;
  }

  if (range_request(ip)) {
    bytes_to_send = range_len(ip);
    assert(range_len(ip) <= stat.st_size);
    rc = lseek(rep->fd, req->range_start, SEEK_SET);
    if (rc < 0) {
      perror("setup_http: lseek failed");
      printf("setup_http: lseek to range_start = %zd failed\n",
        req->range_start);
      exit(1);
    }
    /* If we are using sendfile set the location of the first byte */
    rep->offset = req->range_start;
  } else {
    bytes_to_send = stat.st_size;
    /* If using sendfile we start at the beginning of the file */
    rep->offset = 0;
  }

  rep->total_len = hdr_len + bytes_to_send;
  rep->cur = rep->buf;
  rep->end = rep->cur + hdr_len;

  print_rep_info(rep);

  return SETUP_HTTP_CONTINUE;
} /* setup_static_not_cached */

/*----------------------------------------------------------------------*/

static int
get_version(struct req *req)
{
  int version = -1;

  if (req->close) {
    version = 0;
  } else {
    version = 1;
  }

  return (version);
}

/*----------------------------------------------------------------------*/
int
build_header(struct info *ip, int filesize)
{
  struct rep *rep = 0;
  struct req *req = 0;
  char *type = 0;
  int sd = -1;
  int version = 0;
  int hdr_len = 0;

  req = &ip->req;
  rep = &ip->rep;
  sd = ip->sd;

  if (range_request(ip)) {
    rep->http_status = HTTP_PARTIAL_CONTENT;
    ldbg("is range request\n");
  } else {
    rep->http_status = HTTP_OK;
    ldbg("not range request\n");
  }

  version = get_version(req);
  PRINT_TIME(sd, &tnow, &tprev, "uri = [%s]", req->uri);

  if (options.content_type) {
    type = http_content_type(req->uri);
  } else {
    type = 0;
  }
  ldbg("type = %s\n", type);

  /* First build up the header */
  switch (rep->http_status) {

    case HTTP_OK:
      ldbg("200\n");
      if (options.content_type) {
        hdr_len = snprintf(rep->cur, rep->buf_size,
          HTTP_OK_STR_ID_LEN_TYPE, version,
          server_ident, (unsigned long int) rep->total_len, type);
      } else {
        hdr_len = snprintf(rep->cur, rep->buf_size,
          HTTP_OK_STR_ID_LEN, version, server_ident,
          (unsigned long int) rep->total_len);
      }
      break;

    case HTTP_PARTIAL_CONTENT:
      ldbg("206\n");
      if (rep->cache) {
        /* for range request we can't use the cached header so create one */
        hdr_len = do_range_header(ip, version, filesize, type);
      } else {
        hdr_len = do_range_header(ip, version, filesize, type);
      }
      break;

    default:
      assert(0);
      break;
  }

  assert(hdr_len > 0);
  if (hdr_len >= rep->buf_size) {
    printf("ERROR build_header: write header failed\n");
    printf("ERROR build_header: snprintf returns %d buffer len = %d\n",
      hdr_len, rep->buf_size);
    printf("ERROR build_header: --reply-buffer-size = %d too small\n",
      rep->buf_size);
    printf("must be > %d\n", hdr_len);
    exit(1);
  }

  PRINT_TIME(sd, &tnow, &tprev, "build_headers done hdr_len = %d\n", hdr_len);

  return hdr_len;
}


/*----------------------------------------------------------------------*/
static int
setup_fastcgi(struct info *ip)
{
  struct rep *rep = 0;
  rep = &(ip->rep);
  struct info *peer_ip;
  struct req *peer_req;
  int extra_hdr_len = 0;
  int hdr_len = 0;
  int i = 0;
  char save_char = 0;
  int rc = -1;
  int version = 0;

  rep = &ip->rep;

  version = get_version(&ip->req);

  PRINT_TIME(ip->sd, &tnow, &tprev, "not using cache for FastCGI response");
  rep->use_cache = 0;
  rep->cache = NULL;

  peer_ip = info_ptr(rep->fd);
  assert(peer_ip != NULL);
  if (!(peer_ip->appserver->flags & APPSERVER_SHAREDMEM)) {
    /*
     * In the absence of shared memory, the FastCGI request buffer
     * and the client reply buffer are actually the same buffer.  The
     * FastCGI peer has only been maintaining its own cur and end
     * pointers, however, so we need to update our own pointers
     * before doing anything.
     */
    peer_req = &peer_ip->req;
    rep->cur = peer_req->cur;
    rep->end = peer_req->end;
  }
  rep->total_len = rep->end - rep->cur;

  /*
   * The FastCGI application typically sends some headers of its own,
   * such as Content-Type.  It then must send a blank line to delimit
   * the end of headers.
   * What we do here is as follows.  We scan the FastCGI response
   * until we hit "\r\n\r\n", counting up the number of bytes along
   * the way.  We then strip those bytes from the FastCGI response.
   * Having done so, we now know the "true" content length, and can
   * construct the complete set of headers.
   * This could probably be improved slightly by leaving the extra
   * header bytes in the FastCGI response buffer, thus saving a
   * memcpy().  However, it would complicate our handling of HEAD
   * requests (see below).
   */
  extra_hdr_len = 0;
#define ALLOW_LFLF  // kludge: recognize "\n\n" as well as "\r\n\r\n"
#ifdef ALLOW_LFLF
  for (i = 1; i < (int) rep->total_len; i++) {
    if (rep->cur[i] == '\n' &&
      ((i >= 3 && rep->cur[i - 3] == '\r' && rep->cur[i - 2] == '\n'
          && rep->cur[i - 1] == '\r')
        || rep->cur[i - 1] == '\n')) {
      extra_hdr_len = i + 1;
      break;
    }
  }
#else
  for (i = 3; i < rep->total_len; i++) {
    if (rep->cur[i - 3] == '\r' && rep->cur[i - 2] == '\n'
      && rep->cur[i - 1] == '\r' && rep->cur[i] == '\n') {
      extra_hdr_len = i + 1;
      break;
    }
  }
#endif
  if (extra_hdr_len == 0) {
    //TODO - FastCGI application didn't send blank line
    printf("*** the FastCGI app didn't send a blank line ***\n");
  }
  rep->total_len -= extra_hdr_len;
  if (options.skip_header) {
    rep->cur += extra_hdr_len;
  } else {
    /*
     * In fcgi_service_request(), we've computed the maximum length of
     * the HTTP reply headers that we need to prepend, and left that
     * amount of space (by incrementing cur).  The actual headers
     * typically won't need all of that space, because the content
     * length typically won't require 10 digits to express (this is
     * why we decrease hdr_len by a bit).  Fill in that space with
     * HTTP reply headers.  We use snprintf() for this, to be safe,
     * since we can't afford for any extra data to get overwritten.
     * As it is, there's no way to avoid one character getting
     * overwritten by the trailing nul.  So, we save that character
     * in save_char for subsequent restoration.
     */
    hdr_len = rep->cur - rep->buf;
    hdr_len -= max_ulong_digits() - ulong_digits(rep->total_len);
    save_char = rep->cur[0];
    rep->cur -= hdr_len;
    rc = snprintf(rep->cur, hdr_len + 1, HTTP_OK_STR_ID_LEN_NOBLANK,
      version, server_ident, (unsigned long) rep->total_len);
    if (rc != hdr_len) {
      printf("rc = %d hdr_len = %d\n", rc, hdr_len);
    }
    assert(rc == (int) hdr_len);
    rep->cur[hdr_len] = save_char;
    hdr_len += extra_hdr_len;
    rep->http_status = HTTP_OK;

#ifdef QUEUE_BUFS
    //THIS IS A HACK
    //For some reason, we sometimes enter this branch after the buffer has been
    //transferred to another flow. This causing seg faults and other errors.
    //I currently can't figure out why this is, but the transfer should not
    //be happening if the FCGI request still isn't done. So, in the interest of
    //time, this 'done_write' flag indicates that it has passed this stage and
    //so we can transfer the buffer.
    ip->done_write = 1;
#endif

//#define HDR_CHECK
#ifdef HDR_CHECK
    printf("===================== HEADER INFO ===================\n");
    printf("HEADER: [%s]\n", rep->cur);
    printf("rep->buf = [%s]\n", rep->buf);
    printf("rep->cur = %p\n", rep->cur);
    printf("rep->buf = %p\n", rep->buf);
    printf("hdr_len (calc 1) = %d\n", rep->total_len);
    printf("hdr_len (calc 2) = %d\n",
      max_ulong_digits() - ulong_digits(rep->total_len));
    printf("hdr_len = %d\n", hdr_len);
    printf("extra_hdr_len = %d\n", extra_hdr_len);
    printf("rep->total_len = %d\n", rep->total_len);
    printf("save_char = %c\n", save_char);
    printf("===================== HEADER INFO ===================\n");
#endif
  }

  return SETUP_HTTP_CONTINUE;
} /* setup_fastcgi */


/*----------------------------------------------------------------------*/
static int
work_around_set_value(cacheinfo * cache)
{
  int ret_val = 0;
  if (cache) {
    ret_val = (int) (cache->header_len + cache->buf_len);
  }
  return (ret_val);
}

/*----------------------------------------------------------------------*/
void
print_rep_info(struct rep *rep)
{
  ldbg("rep->iovec[0].iov_base = %p\n", rep->iovec[0].iov_base);
  ldbg("rep->iovec[0].iov_len = %zd\n", rep->iovec[0].iov_len);
  ldbg("rep->iovec[1].iov_base = %p\n", rep->iovec[1].iov_base);
  ldbg("rep->iovec[1].iov_len = %zd\n", rep->iovec[1].iov_len);
  ldbg("rep->offset = %d\n", rep->offset);
  ldbg("rep->total_len = %d\n", rep->total_len);
  ldbg("rep->total_len = %d\n", rep->total_len);
  ldbg("rep->buf = %p\n", rep->buf);
  ldbg("rep->cur = %p\n", rep->cur);
  ldbg("rep->end = %p\n", rep->end);
  ldbg("end - cur = %ld\n", rep->end - rep->cur);
}

/*----------------------------------------------------------------------*/
