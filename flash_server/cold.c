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
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "libhttpd.h"
#include "conn.h"
#include "datacache.h"
#include "handy.h"
#include "cgi.h"
#include "hotname.h"
#include "loop.h"
#include "nameconvert.h"
#include "helper.h"
#include "config.h"
#include "match.h"
#include "hotfile.h"
#include "common.h"

/* ---------------------------------------------------------------- */
static char *
FigureMime(char *expanded, char **type)
{
  /* Figures out MIME encodings and type based on the filename.  Multiple
   ** encodings are separated by semicolons.
   */
  int i, j, k, l;
  int got_enc;
  char *encodings;
  int maxencodings = 0;
  struct table {
    char* ext;
    char* val;
  };
  static struct table enc_tab[] = {
#include "mime_encodings.h"
  };
  static struct table typ_tab[] = {
#include "mime_types.h"
  };
  
  if (!realloc_str(&encodings, &maxencodings, 0))
    return(NULL);
  
  /* Look at the extensions on hc->expnfilename from the back forwards. */
  encodings[0] = '\0';
  i = strlen(expanded);
  for (;;) {
    j = i;
    for (;;) {
      --i;
      if (i <= 0) {
	/* No extensions left. */
	*type = "text/plain";
	return(encodings);
      }
      if (expanded[i] == '.')
	break;
    }
    /* Found an extension. */
    got_enc = 0;
    for (k = 0; k < sizeof(enc_tab)/sizeof(*enc_tab); ++k) {
      l = strlen(enc_tab[k].ext);
      if (l == j - i - 1 &&
	  strncasecmp(&expanded[i+1], enc_tab[k].ext, l) == 0) {
	if (!realloc_str(&encodings, &maxencodings,
			 strlen(enc_tab[k].val) + 1))
	  return(NULL);
	if (encodings[0] != '\0')
	  strcat(encodings, ";");
	strcat(encodings, enc_tab[k].val);
	got_enc = 1;
      }
    }
    if (! got_enc) {
      /* No encoding extension found - time to try type extensions. */
      for (k = 0; k < sizeof(typ_tab)/sizeof(*typ_tab); ++k) {
	l = strlen(typ_tab[k].ext);
	if (l == j - i - 1 &&
	    strncasecmp(&expanded[i+1], typ_tab[k].ext, l) == 0) {
	  *type = typ_tab[k].val;
	  return(encodings);
	}
      }
      /* No recognized type extension found - return default. */
      *type = "text/plain";
      return(encodings);
    }
  }
  return(encodings);		/* not reached, but safe */
}
/* ---------------------------------------------------------------- */
int
ColdFileStuff(httpd_conn *hc, char *expanded, 
	      long fileSize, time_t modTime)
{
  static CacheEntry *ce = NULL;
  char tempSpace[40];

  if (!ce) {
    ce = GetEmptyCacheEntry();
    if (!ce) {
      fprintf(stderr, "out of memory allocating cacheentry\n");
      HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
      return(TRUE);
    }
  }
  
  /* It's just a regular old file.  Send it. */
  ce->ce_file_fd = open(expanded, O_RDONLY);
  if (ce->ce_file_fd < 0) {
    if (errno == ENOENT)
      HttpdSendErr(hc, 404, err404title, err404form, hc->hc_encodedurl);
    else if (errno == EACCES) {
      HttpdSendErr(hc, 403, err403title, err403form, hc->hc_encodedurl);
    }
    else {
      perror("open");
      HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    }
    return(TRUE);
  }
  
  if (modTime == 0) {
    /* we need to stat it ourselves */
    struct stat sb;
    
    if (stat(expanded, &sb) < 0) {
      HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
      return(TRUE);
    }
    fileSize = sb.st_size;
    modTime = sb.st_mtime;
  }
  
  ce->ce_size = fileSize;
  ce->ce_modTime = modTime;
  if (ce->ce_encodings)
    free(ce->ce_encodings);
  ce->ce_encodings = FigureMime(expanded, &ce->ce_type);
  if (CalcRespHeader(ce, 200, ok200title))
    return(TRUE);

  if (accessLoggingEnabled) {
    sprintf(tempSpace, "\" 200 %d\n", (int) fileSize);
    if (ce->ce_200Resp) 
      free(ce->ce_200Resp);
    ce->ce_200Resp = strdup(tempSpace);
    if (!ce->ce_200Resp) 
      fprintf(stderr, "strdup failed in 200 resp\n");
    else
      ce->ce_200RespLen = strlen(ce->ce_200Resp);
  }
  
  if (ce->ce_filename)
    free(ce->ce_filename);
  ce->ce_filename = strdup(expanded);
  
  if (!ce->ce_filename) {
    fprintf(stderr, "out of memory allocating cacheentry filename\n");
    HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    return(TRUE);
  }
  
  if (AddEntryToDataCache(ce)) {
    close(ce->ce_file_fd);
    fprintf(stderr, "add entry failed\n");
    return(TRUE);
  }
  hc->hc_cacheEnt = ce;
  ce = NULL;

  return(FALSE);
}
/* ---------------------------------------------------------------- */
static SRCode 
CGIStuff(httpd_conn *hc, HotNameEntry **hotName, CGIInfo **cgP, 
	 int addEntry) 
{
  if (cgi(hc, hotName, cgP, addEntry))
    return(SR_ERROR);
  return(SR_CGI);
}
/* ---------------------------------------------------------------- */
static SRCode
ProcessColdRequestBackend(httpd_conn* hc)
{
  static char* indexname;
  static int maxindexname = 0;
  int len;
  char* cp;
  char* pi;
  static char *urlDecodeSpace;
  static int urlDecodeSpaceSize = 0;
  char *stripped;
  char *expanded;
  static HotNameEntry *newNameSpace;
  struct stat sb;
  static int maxAltDir;
  static char *altDir;
  static CGIInfo *cgiSpace;
#define TOPLEVELSTRING "./"

  if (urlDecodeSpaceSize == 0 &&
      (!realloc_str(&urlDecodeSpace, &urlDecodeSpaceSize, 0)))
      return(SR_NOMEM);

  if (maxAltDir == 0 &&
      (!realloc_str(&altDir, &maxAltDir, 0)))
      return(SR_NOMEM);

  altDir[0] = '\0';

  /* decode the URL */
  if (!realloc_str(&urlDecodeSpace, &urlDecodeSpaceSize, 
	      strlen(hc->hc_encodedurl)))
    return(SR_NOMEM);
  StrDecode(urlDecodeSpace, hc->hc_encodedurl);

  /* if the request doesn't start with a slash, it's an error */
  if (urlDecodeSpace[0] != '/') {
    HttpdSendErr(hc, 400, err400title, err400form, hc->hc_encodedurl);
    return(SR_ERROR);
  }
  
  if (!cgiSpace) {
    cgiSpace = GetEmptyCGIInfo();
    if (cgiSpace == NULL ||
	realloc_str(&cgiSpace->cgi_query, &cgiSpace->cgi_maxQuery, 0) == NULL ||
	realloc_str(&cgiSpace->cgi_pathInfo, &cgiSpace->cgi_maxPathInfo, 0) == NULL)
      return(SR_NOMEM);
  }
  cgiSpace->cgi_query[0] = '\0';
  cgiSpace->cgi_pathInfo[0] = '\0';
  
  /* if it's in the CGI cache, the decoded URL should differ
     from the stripped name by just the leading slash character */

  hc->hc_hne = FindMatchInHotCGICache(cgiSpace, urlDecodeSpace+1);
  if (hc->hc_hne) 
    return(CGIStuff(hc, &hc->hc_hne, &cgiSpace, FALSE));

  /* at this point, it's not in a hot cache, so we should
     construct its cache entry for handoff later */
  
  if (newNameSpace == NULL && (newNameSpace = GetEmptyHNE()) == NULL)
    return(SR_NOMEM);

  if (!realloc_str(&newNameSpace->hne_encoded,
		   &newNameSpace->hne_maxEncoded,
		   strlen(hc->hc_encodedurl)))
    return(SR_NOMEM);
  strcpy(newNameSpace->hne_encoded, hc->hc_encodedurl);

  stripped = realloc_str(&newNameSpace->hne_stripped, 
			 &newNameSpace->hne_maxStripped, 
			 strlen(hc->hc_encodedurl));
  expanded = realloc_str(&newNameSpace->hne_expanded, 
			 &newNameSpace->hne_maxExpanded, 
			 strlen(hc->hc_encodedurl));
  
  if (!(stripped && expanded))
    return(SR_NOMEM);
  strcpy(stripped, &urlDecodeSpace[1]);
  
  /* Special case for top-level URL. */
  if (stripped[0] == '\0') {
    strcpy(stripped, TOPLEVELSTRING);
  }

  /* Extract query string from encoded URL. */
  cp = strchr(hc->hc_encodedurl, '?');
  if (cp != (char*) 0) {
    ++cp;
    if (!realloc_str(&cgiSpace->cgi_query, &cgiSpace->cgi_maxQuery, 
		     strlen(cp))) 
      return(SR_NOMEM);
    strcpy(cgiSpace->cgi_query, cp);
  }
  /* And remove query from filename. */
  cp = strchr(stripped, '?');
  if (cp != (char*) 0)
    *cp = '\0';
  
  /* Copy original filename to expanded filename. */
  strcpy(expanded, stripped);

  if (hc->hc_stripped) {
    /* we've already done the name conversion */
    free(hc->hc_stripped);
    hc->hc_stripped = NULL;
  }
  else {
    /* copy the name to be converted,
       schedule the conversion, and tell
       the rest of the code to take a hands-off approach,
       as if it were a CGI in progress */
    hc->hc_stripped = strdup(stripped);
    if (!hc->hc_stripped) 
      return(SR_NOMEM);
    ScheduleNameConversion(hc);
    return(SR_CGI);
  }

  /* Tilde mapping. */
  if (expanded[0] == '~') {
    if (TildeMap(hc, &altDir, &maxAltDir)) {
      fprintf(stderr, "tilde map failed\n");
      HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
      return(SR_ERROR);
    }
  }
  
  /* Expand all symbolic links in the filename.  This also gives us
   ** any trailing non-existing components, for pathinfo.
   */
  cp = ExpandSymlinks(expanded, &pi);
  if (cp == (char*) 0) {
    fprintf(stderr, "expanding symlinks gave back NULL\n");
    HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    return(SR_ERROR);
  }

  if (cp[0] == '\0') {
    fprintf(stderr, "expanding symlinks yielded nothing - %s\n", 
	    hc->hc_encodedurl);
    HttpdSendErr(hc, 404, err404title, err404form, hc->hc_encodedurl);
    return(SR_ERROR);
  }

  expanded = realloc_str(&newNameSpace->hne_expanded, 
			 &newNameSpace->hne_maxExpanded, 
			 strlen(cp));
  if (!expanded) 
    return(SR_NOMEM);
  strcpy(expanded, cp);

  if (!realloc_str(&cgiSpace->cgi_pathInfo, &cgiSpace->cgi_maxPathInfo, 
		   strlen(pi)))
    return(SR_NOMEM);
  strcpy(cgiSpace->cgi_pathInfo, pi);
  
  /* Remove pathinfo stuff from the original filename too. */
  if (cgiSpace->cgi_pathInfo[0] != '\0') {
    int i;
    i = strlen(stripped) - strlen(cgiSpace->cgi_pathInfo);
    if (i > 0 && strcmp(&stripped[i], cgiSpace->cgi_pathInfo) == 0)
      stripped[i - 1] = '\0';
  }
  
  /* If the expanded filename is an absolute path, check that it's still
   ** within the current directory or the alternate directory.
   */
  if (expanded[0] == '/') {
    if (strncmp(expanded, HS.cwd, strlen(HS.cwd)) == 0)
      /* Elide the current directory. */
      strcpy(expanded, &expanded[strlen(HS.cwd)]);
    else if (altDir[0] != '\0' &&
	     (strncmp(expanded, altDir, strlen(altDir)) != 0 ||
	      (expanded[strlen(altDir)] != '\0' &&
	       expanded[strlen(altDir)] != '/'))) {
      HttpdSendErr(hc, 403, err403title, err403form, hc->hc_encodedurl);
      return(SR_ERROR);
    }
  }

  /* Stat the file. */
  if (stat(expanded, &sb) < 0) {
    fprintf(stderr, "stating file %s generated error\n", expanded);
    HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    return(SR_ERROR);
  }
  
  /* Is it world-readable?  We check explicitly instead of just
   ** trying to open it, so that no one ever gets surprised by
   ** a file that's not set world-readable and yet somehow is
   ** readable by the HTTP server and therefore the *whole* world.
   */
  if (!(sb.st_mode & S_IROTH)) {
    HttpdSendErr(hc, 403, err403title, err403form, hc->hc_encodedurl);
    return(SR_ERROR);
  }
  
  /* Is it a directory? */
  if (S_ISDIR(sb.st_mode)) { /* is directory */
    if (cgiSpace->cgi_pathInfo[0] != '\0' || 
	cgiSpace->cgi_query[0] != '\0') {
      HttpdSendErr(hc, 404, err404title, err404form, hc->hc_encodedurl);
      return(SR_ERROR);
    }

    /* Special handling for directory URLs that don't end in a slash.
     ** We send back an explicit redirect with the slash, because
     ** otherwise many clients can't build relative URLs properly.
     */
    if (stripped[strlen(stripped)-1] != '/') {
      newNameSpace->hne_type = HNT_REDIRECT;
      hc->hc_hne = newNameSpace;
      EnterIntoHotNameCache(&newNameSpace);
      SendDirRedirect(hc);
      return(SR_ERROR);
    }
    
    /* Check for an index.html file. */
    if (!realloc_str(&indexname, &maxindexname, 
		     strlen(expanded) + 1 + sizeof(INDEX_NAME)))
      return(SR_NOMEM);
    
    strcpy(indexname, expanded);
    len = strlen(indexname);
    if (len == 0 || indexname[len - 1] != '/')
      strcat(indexname, "/");
    strcat(indexname, INDEX_NAME);
    if (stat(indexname, &sb) < 0) {
      /* Nope, no index.html, so it's an actual directory request.
       ** But if there's pathinfo, it's just a non-existent file.
       */
      newNameSpace->hne_type = HNT_LS;
      newNameSpace->hne_modTime = sb.st_mtime;
      hc->hc_hne = newNameSpace;
      EnterIntoHotNameCache(&newNameSpace);
      ScheduleDirHelp(hc);
      return(SR_CGI);
    }
    
    /* Expand symlinks again.  More pathinfo means something went wrong. */
    cp = ExpandSymlinks(indexname, &pi);
    if (cp == (char*) 0) {
      fprintf(stderr, "re-expanding symlinks caused error\n");
      HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
      return(SR_ERROR);
    }
    if (pi[0] != '\0') {
      fprintf(stderr, "re-expanding symlinks caused pathinfo: %s\n",
	      pi);
      HttpdSendErr(hc, 404, err404title, err404form, hc->hc_encodedurl);
      return(SR_ERROR);
    }
    
    expanded = realloc_str(&newNameSpace->hne_expanded, 
			   &newNameSpace->hne_maxExpanded, 
			   strlen(cp));
    if (!expanded) 
      return(SR_NOMEM);
    strcpy(expanded, cp);
    
    /* Now, is the index.html version world-readable? */
    if (!(sb.st_mode & S_IROTH)) {
      HttpdSendErr(hc, 403, err403title, err403form, hc->hc_encodedurl);
      return(SR_ERROR);
    }
  }	/* is directory */
  
  /* Is it world-executable and in the CGI area? */
  if (HS.cgi_pattern && (sb.st_mode & S_IXOTH) &&
      match(HS.cgi_pattern, expanded)) 
    return(CGIStuff(hc, &newNameSpace, &cgiSpace, TRUE));
  
  /* It's not CGI.  If it's executable, or there's
   * a query, someone's trying to either serve or run a non-CGI file
   * as CGI.  Either case is prohibited.
   */
  
  if ((sb.st_mode & S_IXOTH) || (cgiSpace->cgi_query[0] != '\0')) {
    HttpdSendErr(hc, 403, err403title, err403form, hc->hc_encodedurl);
    return(SR_ERROR);
  }

  /* if there's pathinfo, someone mistook a file for a directory */
  if (cgiSpace->cgi_pathInfo[0] != '\0') {
    HttpdSendErr(hc, 404, err404title, err404form, hc->hc_encodedurl);
    return(SR_ERROR);
  }

  newNameSpace->hne_type = HNT_FILE;
  return(RegularFileStuff(hc, &newNameSpace, TRUE, 
			  sb.st_size, sb.st_mtime));
}
/* ---------------------------------------------------------------- */
SRCode
ProcessColdRequest(httpd_conn* hc)
{
  SRCode srRes;

  srRes = ProcessColdRequestBackend(hc);
  if (srRes == SR_NOMEM) {
    fprintf(stderr, "out of memory in ProcessColdRequestBackend\n");
    HttpdSendErr(hc, 500, err500title, err500form, hc->hc_encodedurl);
    return(SR_ERROR);
  }
  return(srRes);
}
/* ---------------------------------------------------------------- */
