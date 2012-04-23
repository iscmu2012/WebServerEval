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


#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "debug.h"
#include "common.h"
#include "options.h"
#include "util.h"

// #define LOCAL_DEBUG
#include "local_debug.h"


/* ---------------------------------------------------------------------- */
req_type_t
analyze_req(struct req *req)
{
  app_t *app;

  assert(req != NULL);
  req->type = REQ_INVALID;
  req->app = NULL;

  /* if there's a question mark in the URI, what follows is a query string */
  if ((req->query_string = strchr(req->uri, '?')) != NULL) {
    *(req->query_string++) = '\0';
  }

  /* Just hard code how to handle php scripts for now */
  if (strstr(req->uri, ".php") != NULL) {
    ldbg("analyze_req: is a php script req->uri = %s\n", req->uri);
    app = find_app("php");
    if (app != NULL) {
      ldbg("analyze_req: found php app, app->uri = %s and req_type = %d\n",
        app->uri, app->req_type);
      req->type = app->req_type;
      req->app = app;
    } else {
      ldbg("analyze_req: did not find php app\n");
    }
    ldbg("analyze_req: found php app and returning\n");
    return req->type;
  }

  if (req->content_len > 0 && req->method != HTTP_POST) {
    /* only POST requests may have content */
    ldbg
      ("analyze_req: looks like a non HTTP_POST but has content, may not be handled properly\n");
  } else if (req->method == HTTP_POST || req->query_string != NULL) {
    /* looks like a dynamic request; find an application to handle it */
    ldbg("analyze_req: looking up app\n");
#ifdef UMEMREDUCTION
    if (options.doc_root_len != 0) {
      printf("--doc-root not supported yet with UMEMREDUCTION\n");
      exit(1);
    }
    app = find_app(req->uri);
#else
    app = find_app(req->uri + options.doc_root_len);
#endif /* UMEMREDUCTION */
    if (app != NULL) {
      ldbg("analyze_req: found app, app->uri = %s and req_type = %d\n",
        app->uri, app->req_type);
      req->type = app->req_type;
      req->app = app;
    } else {
      ldbg("analyze_req: did not find app\n");
    }
  } else {
    ldbg("analyze_req: determined that request is static\n");
    req->type = REQ_STATIC;
  }

  return req->type;
}


/* ---------------------------------------------------------------------- */
int
parse_bytes(struct req *req)
{
  char *cur, *end;
  char *tmp = 0;

#ifdef UMEMREDUCTION
  //position not used
  char stack_tmp[MAX_TMP_LEN];
  char *cookie_space = 0;
  int i = 0;
  char *tmp_space = 0;
  char *uri_space = 0;
#else
  int position;
#endif /* UMEMREDUCTION */
  int rc = -1;                  // assume failure

  assert(req->state != PARSE_DONE);

  cur = req->cur;
  end = req->end;

#ifdef UMEMREDUCTION
  //if tmp has not been allocated for this request, use the buffer on the stack
  if (!req->tmp_allocated) {
    req->tmp = stack_tmp;
    ldbg("parse_bytes: Using stack based tmp buffer %p\n", req->tmp);
  } else {
    ldbg("parse_bytes: Using heap based tmp buffer %p\n", req->tmp);
  }
#endif /* UMEMREDUCTION */

  while (cur < end && req->state != PARSE_DONE) {
    if (req->skip_lws) {
      if (*cur == ' ' || *cur == '\t') {
        ldbg("parse_bytes: skipping whitespace\n");
        cur++;
        continue;
      } else {
        req->skip_lws = 0;
      }
    }
    ldbg("parse_bytes: *cur = [%s] req->state = %d\n",
      nice_char(*cur), req->state);
    ldbg("parse_bytes: req->tmp_len = %d buf = [%s] req->tmp = [%s]\n",
      req->tmp_len,
      nice_strn(cur, end - cur), nice_strn(req->tmp, req->tmp_len));

    switch (req->state) {
      case PARSE_HEADER_NAME:
        while (cur < end && *cur != ':' && !isspace((int) *cur)) {
          req->tmp[req->tmp_len++] = *cur++;
#ifdef UMEMREDUCTION
          if (req->tmp_len >= MAX_TMP_LEN) {
#else
          if (req->tmp_len >= (int) sizeof(req->tmp)) {
#endif /* UMEMREDUCTION */
            ldbg("parse_bytes: req->tmp_len too large\n");
            goto parse_bytes_done;
          }
        }
        if (cur == end) {
          ldbg("parse_bytes: end of buf reached\n");
          break;
        }

        req->tmp[req->tmp_len] = '\0';
        req->tmp_len = 0;
        ldbg("parse_bytes: req->tmp = [%s]\n", nice_str(req->tmp));

        if (*cur == '\r' || *cur == '\n') {
          if (req->tmp_len == 0) {
            /* special case: an empty line marks end of headers */
            req->state = PARSE_CR_2;
          } else {
            req->state = PARSE_CR_1;
          }
        } else {
          req->state = PARSE_COLON;
          req->skip_lws = 1;
        }
        break;

      case PARSE_COLON:
        if (*cur == ':') {
          ldbg("parse_bytes: PARSE_COLON got COLON\n");
          /* header names are NOT case sensitive -- use stricmp() */
          if (stricmp(req->tmp, "Content-Length") == 0) {
            req->state = PARSE_CONTENT_LENGTH;
            req->skip_lws = 1;
          } else if (stricmp(req->tmp, "Range") == 0) {
            req->state = PARSE_RANGE_REQUEST;
            req->skip_lws = 1;
          } else if (stricmp(req->tmp, "If-Modified-Since") == 0) {
            req->state = PARSE_IF_MODIFIED_SINCE;
            req->skip_lws = 1;
#ifdef CALL_STATS
          } else if (stricmp(req->tmp, "Client-Id") == 0) {
            req->state = PARSE_CLIENT_ID;
            req->skip_lws = 1;
#endif /* CALL STATS */
          } else if (stricmp(req->tmp, "Cookie") == 0) {
            if (req->cookie_len > 0) {
#ifdef UMEMREDUCTION
              /* if a second cookie header is specified, a heap buffer must be
               * allocated for the cookies 
               */
              if (!req->cookie_allocated) {

                // space must be allocated
                ldbg("parse_bytes: allocating cookie space\n");

                // TODO
                // Change memory allocation to use internal management
                // allocate space for cookie 
                cookie_space = malloc(sizeof(char) * MAX_COOKIE_LEN);

                if (cookie_space == NULL) {
                  printf
                    ("parse_bytes: unable to allocate space for cookie\n");
                  goto parse_bytes_done;
                }
                //copy data to new buffer
                for (i = 0; i < req->cookie_len; i++) {
                  cookie_space[i] = req->cookie[i];
                }
                //set cookie as allocated
                req->cookie = cookie_space;
                req->cookie_allocated = 1;
              }
              /* set up cookie buffer to append the new cookie */
              if (req->cookie_len + 2 >= MAX_COOKIE_LEN) {
                ldbg("parse_bytes: req->cookie_len too large\n");
                goto parse_bytes_done;
              }
              req->cookie[req->cookie_len++] = ';';
              req->cookie[req->cookie_len++] = ' ';
#else
              /* append the new cookie to the end of existing cookie */
              if (req->cookie_len + 2 >= (int) sizeof(req->cookie)) {
                ldbg("parse_bytes: req->cookie_len too large\n");
                goto parse_bytes_done;
              }
              req->cookie[req->cookie_len++] = ';';
              req->cookie[req->cookie_len++] = ' ';
#endif /* UMEMREDUCTION */
            }
#ifdef UMEMREDUCTION
            else if (!req->cookie_allocated) {
              ldbg("parse_bytes: first cookie, cookie is unallocated\n");
              //this is the first cookie header specified and the cookie pointer has not been set yet
              //note: add 2 because cur is pointing at the colon
              req->cookie = cur + 2;
            } else {
              ldbg("parse_bytes: first cookie, cookie is allocated\n");
            }
#endif /* UMEMREDUCTION */
            req->state = PARSE_COOKIE;
            req->skip_lws = 1;
          } else {
            ldbg("parse_bytes: unrecognized header [%s]\n",
              nice_str(req->tmp));
            req->state = PARSE_SKIP;
          }
          cur++;
        } else {
          ldbg("parse_bytes: expected colon\n");
          req->state = PARSE_SKIP;
        }
        break;

      case PARSE_COOKIE:
        while (cur < end) {
          ldbg("parse_bytes: PARSE_COOKIE *cur = [%s]\n", nice_char(*cur));
          if (*cur == '\r' || *cur == '\n') {
            ldbg("parse_bytes: PARSE_COOKIE done\n");
            req->state = PARSE_CR_1;
            break;
          } else {
#ifdef UMEMREDUCTION
            if (req->cookie_allocated) {
              req->cookie[req->cookie_len++] = *cur++;
              if (req->cookie_len >= MAX_COOKIE_LEN) {
                ldbg("parse_bytes: req->cookie_len too large\n");
                goto parse_bytes_done;
              }
            } else {
              cur++;
              req->cookie_len++;
            }
#else
            req->cookie[req->cookie_len++] = *cur++;
            if (req->cookie_len >= (int) sizeof(req->cookie)) {
              ldbg("parse_bytes: req->cookie_len too large\n");
              goto parse_bytes_done;
            }
#endif /* UMEMREDUCTION */
          }
        }
        break;

      case PARSE_IF_MODIFIED_SINCE:
        while (cur < end) {
          ldbg("parse_bytes: PARSE_IF_MODIFIED_SINCE *cur = [%s]\n",
            nice_char(*cur));
          if (*cur == '\r' || *cur == '\n') {
            ldbg("parse_bytes: PARSE_IF_MODIFIED_SINCE done\n");
            req->tmp[req->tmp_len] = '\0';
            req->tmp_len = 0;
            if (!http_str_to_time(req->tmp, &req->ifmodsince)
              && req->ifmodsince) {
              ldbg("parse_bytes: date conversion failed [%s]\n", req->tmp);
              goto parse_bytes_done;
            }
            req->state = PARSE_CR_1;
            break;
          } else {
            req->tmp[req->tmp_len++] = *cur++;
#ifdef UMEMREDUCTION
            if (req->tmp_len >= MAX_TMP_LEN) {
#else
            if (req->tmp_len >= (int) sizeof(req->tmp)) {
#endif /* UMEMREDUCTION */
              ldbg("parse_bytes: req->tmp_len too large\n");
              goto parse_bytes_done;
            }
          }
        }
        break;

#ifdef CALL_STATS
      case PARSE_CLIENT_ID:
        while (cur < end && !isspace((int) *cur)) {
          if (isdigit((int) *cur)) {
            req->client_id = (req->client_id * 10) + (*cur - '0');
            if (req->client_id > MAX_CLIENT_ID) {
              ldbg("parse_bytes: client id too large\n");
              goto parse_bytes_done;
            }
            cur++;
          } else {
            ldbg("parse_bytes: client_id invalid char [%s] in Client Id\n",
              nice_char(*cur));
            goto parse_bytes_done;
          }
        }

        /* skip over spaces to get to the next number */
        while (isspace((int) *cur)) {
          ldbg("parse_bytes: client_id skipping space\n");
          cur++;
        }

        while (cur < end && !isspace((int) *cur)) {
          if (isdigit((int) *cur)) {
            req->call_id = (req->call_id * 10) + (*cur - '0');
            if (req->call_id > MAX_CLIENT_ID) {
              ldbg("parse_bytes: client id too large\n");
              goto parse_bytes_done;
            }
            cur++;
          } else {
            ldbg("parse_bytes: client_id invalid char [%s] in Client Id\n",
              nice_char(*cur));
            goto parse_bytes_done;
          }
        }


        if (cur == end) {
          ldbg("parse_bytes: client_id end of buf reached\n");
          break;
        }

        ldbg("parse_bytes: Client Id is %d call id is %d\n", req->client_id,
          req->call_id);
        req->state = PARSE_HEADER_NAME;
        break;
#endif /* CALL_STATS */

        /* Range request looks like:
         * Range: bytes=0-10
         * This asks for bytes 0 to 10 inclusive (i.e., 11 bytes)
         */
      case PARSE_RANGE_REQUEST:
        /* This is a range request */
        req->range_request = 1;

        /* first skip over the bytes = part */
        ldbg("cur = %s\n", cur);
        ldbg("looking for and skipping bytes=\n");
        tmp = strstr(cur, "bytes=");
        ldbg("tmp = %s\n", tmp);
        if (tmp == NULL) {
          printf("Ill defined range request\n");
          printf("String = %s\n", cur);
          printf("Range requests must be of the form Range: bytes=0-10\n");
          exit(1);
        } else {
          cur += strlen("bytes=");
          ldbg("Moving to position after bytes= now cur = %s\n", cur);
        }

        while (cur < end && !isspace((int) *cur) && *cur != '-') {
          ldbg("parse_bytes doing start cur = %c\n", *cur);
          if (isdigit((int) *cur)) {
            req->range_start = (req->range_start * 10) + (*cur - '0');
            cur++;
          } else {
            ldbg("parse_bytes: range start invalid char [%s] in Range\n",
              nice_char(*cur));
            goto parse_bytes_done;
          }
        }


        if (*cur != '-') {
          goto parse_bytes_done;
        } else {
          cur++;
        }

        while (cur < end && !isspace((int) *cur)) {
          ldbg("parse_bytes doing end cur = %c\n", *cur);
          if (isdigit((int) *cur)) {
            req->range_end = (req->range_end * 10) + (*cur - '0');
            cur++;
          } else {
            ldbg("parse_bytes: range end invalid char [%s] in Range\n",
              nice_char(*cur));
            goto parse_bytes_done;
          }
        }

        if (cur == end) {
          ldbg("parse_bytes: range request end of buf reached\n");
          break;
        }

        ldbg("parse_bytes: range request start = %zd end = %zd\n",
          req->range_start, req->range_end);


        req->state = PARSE_HEADER_NAME;
        break;

      case PARSE_CONTENT_LENGTH:
        while (cur < end && !isspace((int) *cur)) {
          if (isdigit((int) *cur)) {
            req->content_len = (req->content_len * 10) + (*cur - '0');
            if (req->content_len > MAX_CONTENT_LEN) {
              ldbg("parse_bytes: content-length too large\n");
              goto parse_bytes_done;
            }
            cur++;
          } else {
            ldbg("parse_bytes: invalid char [%s] in content-length\n",
              nice_char(*cur));
            goto parse_bytes_done;
          }
        }
        if (cur == end) {
          ldbg("parse_bytes: end of buf reached\n");
          break;
        }

        ldbg("parse_bytes: content length is %d\n", req->content_len);
        req->state = PARSE_SKIP;
        break;

      case PARSE_SKIP:
        /* skip over any characters until we get to CR or LF */
        while (cur < end) {
          ldbg("parse_bytes: PARSE_SKIP *cur = [%s]\n", nice_char(*cur));
          if (*cur == '\r' || *cur == '\n') {
            ldbg("parse_bytes: PARSE_SKIP got rid of characters\n");
            req->state = PARSE_CR_1;
            break;
          } else {
            cur++;
          }
        }
        break;

      case PARSE_CR_1:
      case PARSE_CR_2:
        ldbg("parse_bytes: PARSE_CR *cur = [%s]\n", nice_char(*cur));
        if (*cur == '\r') {
          if (req->state == PARSE_CR_1) {
            req->state = PARSE_LF_1;
          } else {
            req->state = PARSE_LF_2;
          }
          ldbg("parse_bytes: PARSE_CR got CR\n");
        } else {
          req->state = PARSE_SKIP;
        }
        cur++;
        break;

      case PARSE_LF_1:
      case PARSE_LF_2:
        ldbg("parse_bytes: PARSE_LF *cur = [%s]\n", nice_char(*cur));
        if (*cur == '\n') {
          ldbg("parse_bytes: PARSE_LF got LF\n");
          switch (req->method) {
            case HTTP_NONE:
              /*
               * We got a blank line where we were expecting a method.
               * Uncomment one of the two following lines to either allow
               * it or return an error.
               */
              req->state = PARSE_METHOD;  // allow it
              //goto parse_bytes_done;    // return an error
              break;
            case HTTP_SIMPLE:
              /* the request is equivalent to "GET uri HTTP/1.0" */
              req->method = HTTP_GET;
              /* explicitly forbid headers */
              req->state = PARSE_DONE;
              break;
            default:
              if (req->state == PARSE_LF_1) {
                req->state = PARSE_HEADER_NAME;
              } else {
                req->state = PARSE_DONE;
              }
              break;
          }
        } else {
          req->state = PARSE_SKIP;
        }
        cur++;
        break;

      case PARSE_METHOD:
        while (cur < end && !isspace((int) *cur)) {
          req->tmp[req->tmp_len++] = *cur++;
#ifdef UMEMREDUCTION
          if (req->tmp_len >= MAX_TMP_LEN) {
#else
          if (req->tmp_len >= (int) sizeof(req->tmp)) {
#endif /* UMEMREDUCTION */
            ldbg("parse_bytes: req->tmp_len too large\n");
            goto parse_bytes_done;
          }
        }
        if (cur == end) {
          ldbg("parse_bytes: end of buf reached\n");
          break;
        }

        req->tmp[req->tmp_len] = '\0';
        req->tmp_len = 0;
        ldbg("parse_bytes: req->tmp = [%s]\n", nice_str(req->tmp));

        /* methods ARE case sensitive -- use strcmp() */
        if (strcmp(req->tmp, "GET") == 0) {
          req->method = HTTP_GET;
          ldbg("parse_bytes: GET\n");
        } else if (strcmp(req->tmp, "HEAD") == 0) {
          req->method = HTTP_HEAD;
          ldbg("parse_bytes: HEAD\n");
        } else if (strcmp(req->tmp, "POST") == 0) {
          req->method = HTTP_POST;
          ldbg("parse_bytes: POST\n");
        } else if (req->tmp[0] == '\0') {
          /*
           * We got a blank line (possibly with whitespace) where we were
           * expecting a method.  We'll handle this condition later, once
           * we've processed the linefeed.
           */
          req->state = PARSE_CR_1;
          break;
        } else {
          ldbg("parse_bytes: unrecognized method [%s]\n", nice_str(req->tmp));

          req->method = HTTP_NOT_IMPL;
        }
        req->skip_lws = 1;
        req->state = PARSE_URI;
        break;

      case PARSE_URI:
        /* ignore leading slash */
        if (*cur == '/') {
          ldbg("parse_bytes: parsing uri *cur = [%s]\n", nice_char(*cur));
          cur++;
        }
#ifdef UMEMREDUCTION
        if (!req->uri_allocated) {
          ldbg("parse_bytes: uri in read buffer\n");
          req->uri = cur;
        } else {
          ldbg("parse_bytes: uri in sperate buffer\n");
        }
#endif /* UMEMREDUCTION */

        while (cur < end && !isspace((int) *cur)) {
          ldbg("parse_bytes: parsing uri *cur = [%s]\n", nice_char(*cur));

#ifdef UMEMREDUCTION
          //if the uri is pointing into the read buffer
          if (!req->uri_allocated) {
            //shift the pointer
            req->uri_len++;
            cur++;
          } else {
            //otherwise, the space has been allocated for the uri
            //copy the character
            req->uri[req->uri_len++] = *cur++;
            //check for overflow
            if (req->uri_len >= MAX_URI_LEN) {
              ldbg
                ("parse_bytes: uri too large, consider increasing MAX_URI_LEN\n");
              goto parse_bytes_done;
            }
          }
#else
          position = options.doc_root_len + req->tmp_len;
          req->uri[position] = *cur++;
          req->tmp_len++;

          if (options.doc_root_len + req->tmp_len >= (int) sizeof(req->uri)) {
            ldbg("parse_bytes: req->tmp_len too large\n");
            goto parse_bytes_done;
          }
#endif /* UMEMREDUCTION */
        }
        if (cur == end) {
          ldbg("parse_bytes: end of buf reached\n");
          break;
        }
#ifdef UMEMREDUCTION
        // TODO
        // This appears to be done later in the code now.
        //place '\0' at the end of the uri after parsing the entire request
#else
        req->uri[options.doc_root_len + req->tmp_len] = '\0';
#endif /* UMEMREDUCTION */
        req->tmp_len = 0;
        req->skip_lws = 1;
        req->state = PARSE_VERSION;
        ldbg("parse_bytes: parsing uri state now %d\n", req->state);
        break;

      case PARSE_VERSION:
        while (cur < end && !isspace((int) *cur)) {
          req->tmp[req->tmp_len++] = *cur++;
#ifdef UMEMREDUCTION
          if (req->tmp_len >= MAX_TMP_LEN) {
#else
          if (req->tmp_len >= (int) sizeof(req->tmp)) {
#endif /* UMEMREDUCTION */
            ldbg("parse_bytes: req->tmp_len too large\n");
            goto parse_bytes_done;
          }
        }
        if (cur == end) {
          ldbg("parse_bytes: end of buf reached\n");
          break;
        }

        req->tmp[req->tmp_len] = '\0';
        req->tmp_len = 0;
        ldbg("parse_bytes: req->tmp = [%s]\n", nice_str(req->tmp));

        if (strcmp(req->tmp, "HTTP/1.1") == 0) {
          ldbg("parse_bytes: HTTP/1.1\n");
          req->close = 0;
        } else if (strcmp(req->tmp, "HTTP/1.0") == 0) {
          ldbg("parse_bytes: HTTP/1.0\n");
          req->close = 1;
        } else if (req->tmp[0] == '\0') {
          /*
           * HTTP/1.0 and earlier allow "simple" requests that omit the
           * version field.  These requests are supposed to be limited to
           * the GET method.
           */
          if (req->method != HTTP_GET) {
            ldbg("parse_bytes: simple requests only support GET method\n");
            goto parse_bytes_done;
          }
          ldbg("parse_bytes: simple HTTP/1.0\n");
          /*
           * We store the method as HTTP_SIMPLE for now, as a hint to our
           * subsequent parsing logic (we need to know to forbid headers).
           * After parsing, the method will revert to HTTP_GET.
           */
          req->method = HTTP_SIMPLE;
          req->close = 1;
        } else {
          ldbg("parse_bytes: HTTP version [%s] unknown\n",
            nice_str(req->tmp));
          goto parse_bytes_done;
        }
        req->state = PARSE_SKIP;
        ldbg("parse_bytes: state now %d\n", req->state);
        break;

      default:
        fprintf(stderr, "Request state %d is invalid\n", req->state);
        exit(1);
    } /* switch */
  } /* while */

  if (req->state == PARSE_DONE) {
    ldbg("parse_bytes: parse done\n");
#ifdef UMEMREDUCTION
    // TODO
    // Do we need to check if the buffers are full and
    // then we try to write passed the end
    // both for cookie and uri.
    if (req->cookie_len > 0) {
      req->cookie[req->cookie_len] = '\0';
      ldbg("parse_bytes: cookie: %s\n", req->cookie);
    }
    req->uri[req->uri_len] = '\0';
#else
    req->cookie[req->cookie_len] = '\0';
#endif /* UMEMREDUCTION */
    analyze_req(req);
    PRINT_TIME(NOFD, &tnow, &tprev, "req->type = %d", req->type);
    if (req->type == REQ_INVALID) {
      printf("parse_bytes: unable to determine type of request\n");
      assert(0);
      req->type = REQ_STATIC;
      goto parse_bytes_done;
    }
  }
#ifdef UMEMREDUCTION
  // if the request has not been completely parsed due to the request string
  // overflowing the read buffer, tmp and uri must be allocated 
  else if (cur == end) {

    // tmp and uri are only allocated once
    // if tmp has not been previously allocated and must now be allocated
    if (!req->tmp_allocated) {

      ldbg("parse_bytes: allocating tmp space\n");

      // allocate space for tmp
      tmp_space = malloc(sizeof(char) * MAX_TMP_LEN);

      if (tmp_space == NULL) {
        printf("parse_bytes: unable to allocate space for tmp\n");
        goto parse_bytes_done;
      }
      // copy data to new buffer
      for (i = 0; i < req->tmp_len; i++) {
        tmp_space[i] = req->tmp[i];
      }

      // TODO
      // Check with Tyler on the order of this.
      // Seems a bit late to be now saying it is allocated.

      // set tmp as allocated
      req->tmp = tmp_space;
      req->tmp_allocated = 1;

    }
    // if uri has not been previously allocated and must now be allocated
    if (!req->uri_allocated) {

      ldbg("parse_bytes: allocating uri space\n");

      //allocate space for uri
      uri_space = malloc(sizeof(char) * MAX_URI_LEN);
      if (uri_space == NULL) {
        printf("parse_bytes: unable to allocate space for uri\n");
        goto parse_bytes_done;
      }
      // copy data to new buffer
      for (i = 0; i < req->uri_len; i++) {
        uri_space[i] = req->uri[i];
      }

      // TODO
      // Check that we haven't first reached the end of the buffer.
      // add null terminator for edge case
      uri_space[i] = '\0';

      // set uri as allocated
      req->uri = uri_space;
      req->uri_allocated = 1;
    }
    // if cookie has not been previously allocated and must now be allocated
    if (!req->cookie_allocated) {

      ldbg("parse_bytes: allocating cookie space\n");

      // allocate space for cookie 
      cookie_space = malloc(sizeof(char) * MAX_COOKIE_LEN);
      if (cookie_space == NULL) {
        printf("parse_bytes: unable to allocate space for cookie\n");
        goto parse_bytes_done;
      }
      // copy data to new buffer

      for (i = 0; i < req->cookie_len; i++) {
        cookie_space[i] = req->cookie[i];
      }

      // add null terminator for edge case
      cookie_space[i] = '\0';

      // set cookie as allocated
      req->cookie = cookie_space;
      req->cookie_allocated = 1;
    }
  }
#endif /* UMEMREDUCTION */

  rc = 0; // everything's OK

parse_bytes_done:
  ldbg("parse_bytes: returning 0 state = %d\n", req->state);
  ldbg("parse_bytes: loop req->tmp [%s]\n", nice_strn(req->tmp,
      req->tmp_len));
  /*
   * For convenience, and speed, we've been using 'cur' instead of 'req->cur'
   * throughout this function.  Now we must update req->cur before returning.
   */
  if (cur == end) {
    req->end = req->cur = req->buf;
  } else {
    req->cur = cur;
  }
  return rc;
}
