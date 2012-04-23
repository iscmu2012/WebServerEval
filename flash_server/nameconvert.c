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


#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "conn.h"
#include "libhttpd.h"
#include "handy.h"
#include "config.h"

/* ---------------------------------------------------------------- */
static int
IsHexit(char c)
{
  if (strchr("0123456789abcdefABCDEF", c) != (char*) 0)
    return 1;
  return 0;
}
/* ---------------------------------------------------------------- */
static int
Hexit(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return 0;		/* shouldn't happen, we're guarded by is_hexit() */
}
/* ---------------------------------------------------------------- */
void
StrDecode(char* to, char* from)
{
  /* Copies and decodes a string.  It's ok for from and to to be the
   ** same string.
   */
  for (; *from != '\0'; ++to, ++from) {
    if (from[0] == '%' && IsHexit(from[1]) && IsHexit(from[2])) {
      *to = Hexit(from[1]) * 16 + Hexit(from[2]);
      from += 2;
    }
    else
      *to = *from;
  }
  *to = '\0';
}
/* ---------------------------------------------------------------- */
int
TildeMap(httpd_conn* hc, char **altDir, int *maxAltDir)
{
  /* Map a ~username/whatever URL into something else.  
     Two different ways. */

#ifdef TILDE_MAP_1
#ifdef TILDE_MAP_2
#error "both TILDE_MAP_1 and TILDE_MAP_2 are defined in config.h"
#endif
#endif

#if defined(TILDE_MAP_1) || defined(TILDE_MAP_2)
  static char* temp;
  static int maxtemp = 0;
#endif
#ifdef TILDE_MAP_1
  /* Map ~username to <prefix>/username. */
  int len;
  static char* prefix = TILDE_MAP_1;
  
  len = strlen(hc->expnfilename) - 1;
  if (!realloc_str(&temp, &maxtemp, len))
    return(TRUE);
  strcpy(temp, &hc->expnfilename[1]);
  if (!realloc_str(&hc->expnfilename, &hc->maxexpnfilename, 
		   strlen(prefix) + 1 + len))
    return(TRUE);
  strcpy(hc->expnfilename, prefix);
  if (prefix[0] != '\0')
    strcat(hc->expnfilename, "/");
  strcat(hc->expnfilename, temp);
#endif /* TILDE_MAP_1 */
  
#ifdef TILDE_MAP_2
  /* Map ~username to <user's homedir>/<postfix>. */
  static char* postfix = TILDE_MAP_2;
  char* cp;
  struct passwd* pw;
  
  /* Get the username. */
  if (!realloc_str(&temp, &maxtemp, strlen(hc->expnfilename) - 1))
    return(TRUE);
  strcpy(temp, &hc->expnfilename[1]);
  cp = strchr(temp, '/');
  if (cp != (char*) 0)
    *cp++ = '\0';
  else
    cp = "";
  
  /* Get the passwd entry. */
  pw = getpwnam(temp);
  if (pw == (struct passwd*) 0)
    return(TRUE);
  
  /* Set up altdir. */
  if (!realloc_str(altDir, maxAltDir, 
		   strlen(pw->pw_dir) + 1 + strlen(postfix)))
    return(TRUE);
  strcpy(*altDir, pw->pw_dir);
  if (postfix[0] != '\0') {
    strcat(*altDir, "/");
    strcat(*altDir, postfix);
  }
  
  /* And the filename becomes altdir plus the post-~ part of the original. */
  if (!realloc_str(&hc->expnfilename, &hc->maxexpnfilename,
		   strlen(*altDir) + 1 + strlen(cp)))
    return(TRUE);
  sprintf(hc->expnfilename, "%s/%s", *altDir, cp);
#endif /* TILDE_MAP_2 */
  return(FALSE);
}
/* ---------------------------------------------------------------- */
/* Expands all symlinks in the given filename, eliding ..'s.  Returns the
** expanded path (pointer to static string), or (char*) 0 on errors.
** Also returns, in the string pointed to by restP, any trailing parts of
** the path that don't exist.
**
** This is a fairly nice little routine.  It handles any size filenames
** without excessive mallocs.
*/
/* ---------------------------------------------------------------- */
char *
ExpandSymlinks(char* path, char** restP)
{
  static char* checked;
  static char* rest;
  char link[5000];
  static int maxchecked = 0, maxrest = 0;
  int checkedlen, restlen, linklen, prevcheckedlen, prevrestlen, nlinks, i;
  char* r;
  char* cp1;
  char* cp2;
  
  /* Start out with nothing in checked and the whole filename in rest. */
  if (!realloc_str(&checked, &maxchecked, 0))
    return(NULL);
  checked[0] = '\0';
  checkedlen = 0;
  restlen = strlen(path);
  if (!realloc_str(&rest, &maxrest, restlen))
    return(NULL);
  strcpy(rest, path);
  if (rest[restlen - 1] == '/')
    rest[--restlen] = '\0';		/* trim trailing slash */
  /* Remove any leading slashes. */
  while ( rest[0] == '/' )
    {
      (void) strcpy( rest, &(rest[1]) );
      --restlen;
    }
  r = rest;
  nlinks = 0;
  
  /* While there are still components to check... */
  while (restlen > 0) {
    /* Save current checkedlen in case we get a symlink.  Save current
    ** restlen in case we get a non-existant component.
    */
    prevcheckedlen = checkedlen;
    prevrestlen = restlen;
    
    /* Grab one component from r and transfer it to checked. */
    cp1 = strchr(r, '/');
    if (cp1 != (char*) 0) {
      i = cp1 - r;
      if (i == 0) {
	/* Special case for absolute paths. */
	if (!realloc_str(&checked, &maxchecked, checkedlen + 1))
	  return(NULL);
	strncpy(&checked[checkedlen], r, 1);
	checkedlen += 1;
      }
      else if (strncmp(r, "..", MAX(i, 2)) == 0) {
	/* Ignore ..'s that go above the start of the path. */
	if (checkedlen != 0) {
	  cp2 = strrchr(checked, '/');
	  if (cp2 == (char*) 0)
	    checkedlen = 0;
	  else if (cp2 == checked)
	    checkedlen = 1;
	  else
	    checkedlen = cp2 - checked;
	}
      }
      else {
	if (!realloc_str(&checked, &maxchecked, checkedlen + 1 + i))
	  return(NULL);
	if (checkedlen > 0 && checked[checkedlen-1] != '/')
	  checked[checkedlen++] = '/';
	strncpy(&checked[checkedlen], r, i);
	checkedlen += i;
      }
      checked[checkedlen] = '\0';
      r += i + 1;
      restlen -= i + 1;
    }
    else {
      /* No slashes remaining, r is all one component. */
      if (strcmp(r, "..") == 0) {
	/* Ignore ..'s that go above the start of the path. */
	if (checkedlen != 0) {
	  cp2 = strrchr(checked, '/');
	  if (cp2 == (char*) 0)
	    checkedlen = 0;
	  else if (cp2 == checked)
	    checkedlen = 1;
	  else
	    checkedlen = cp2 - checked;
	  checked[checkedlen] = '\0';
	}
      }
      else {
	if (!realloc_str(&checked, &maxchecked, 
			 checkedlen + 1 + restlen))
	  return(NULL);
	if (checkedlen > 0 && checked[checkedlen-1] != '/')
	  checked[checkedlen++] = '/';
	strcpy(&checked[checkedlen], r);
	checkedlen += restlen;
      }
      r += restlen;
      restlen = 0;
    }
    
    /* Try reading the current filename as a symlink */
    linklen = readlink(checked, link, sizeof(link));
    if (linklen == -1) {
      if (errno == EINVAL)
	continue;		/* not a symlink */
      if (errno == EACCES || errno == ENOENT || errno == ENOTDIR) {
	/* That last component was bogus.  Restore and return. */
	*restP = r - (prevrestlen - restlen);
	if ( prevcheckedlen == 0 )
	  (void) strcpy( checked, "." );
	else
	  checked[prevcheckedlen] = '\0';
	return checked;
      }
      perror("readlink");
      return (char*) 0;
    }
    ++nlinks;
    if (nlinks > MAX_LINKS) {
      fprintf(stderr, "too many symlinks");
      return (char*) 0;
    }
    link[linklen] = '\0';
    if (link[linklen - 1] == '/')
      link[--linklen] = '\0';	/* trim trailing slash */
    
    /* Insert the link contents in front of the rest of the filename. */
    if (restlen != 0) {
      strcpy(rest, r);
      if (!realloc_str(&rest, &maxrest, restlen + linklen + 1))
	return(NULL);
      for (i = restlen; i >= 0; --i)
	rest[i + linklen + 1] = rest[i];
      strcpy(rest, link);
      rest[linklen] = '/';
      restlen += linklen + 1;
      r = rest;
    }
    else {
      /* There's nothing left in the filename, so the link contents
      ** becomes the rest.
      */
      if (!realloc_str(&rest, &maxrest, linklen))
	return(NULL);
      strcpy(rest, link);
      restlen = linklen;
      r = rest;
    }
    
    /* Re-check this component. */
    checkedlen = prevcheckedlen;
    checked[checkedlen] = '\0';
  }
  
  /* Ok. */
  *restP = r;
  if ( checked[0] == '\0' )
    (void) strcpy( checked, "." );
  return checked;
}
/* ---------------------------------------------------------------- */
