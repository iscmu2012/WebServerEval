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


/* 
   The file, redirect, and ls hot cache is combined
   It hashes on the encoded url

   the CGI cache is separate, and uses the stripped URL.
   the encoded url for CGI entries is basically garbage

   when we receive a URL, it may be encoded, using escaped 
   characters we keep this around in the per-connection 
   information

   we then decode it (get rid of escaped characters) and
   strip off the first character (the '/') and any query
   information (anything after '?'). We also remove any
   pathinfo (nonexistent components in URL) and keep them
   aside.

   what remains is a "stripped", and is either the name
   of a real file (or directory), or the name of a CGI program.
   Logically, we then take the stripped and expand it to
   a real filename on disk. This becomes the "expandedName".
   In reality, we need to expand first to determine the pathInfo,
   which then gets removed from the stripped, but that's
   just a minor detail.

   in the case of files, the presence of pathinfo is an error
     because it means that the user mistook a filename for a
     directory name
   in the case of directories, the presence of pathinfo is an
     error because it means that the file/subdirectory doesn't
     exist
   in the case of CGI applications, pathinfo is normal and OK

   Likewise, we will accept queries only for CGI apps and generate
     errors if they are used for files or directories

   No decodedurl in a HotNameEntry can contain contain either
     pathinfo or a query as a result of our conditions. This
     also prevents us from having errors in matching
*/

#define NAMECACHELIFETIME 600

typedef enum HotNameTypes {
  HNT_FILE, 
  HNT_REDIRECT, 
  HNT_LS, 
  NUM_HNT} HotNameTypes;

typedef struct HotNameEntry {
  int hne_refCount;
  char *hne_encoded;		/* the original URL */
  long hne_time;		/* when this entry was entered */
  struct HotNameEntry *hne_next;
  struct HotNameEntry **hne_prev;
  char *hne_stripped;		/* decoded, void of query or pathinfo */
  char *hne_expanded;		/* symlinks expanded */
  int hne_maxEncoded;
  int hne_maxStripped;
  int hne_maxExpanded;
  HotNameTypes hne_type;
  int hne_modTime;		/* currently used only by "ls" */
  struct HotNameEntry *hne_nextMRU; /* if refcount is zero, it's on MRU list */
  struct HotNameEntry *hne_prevMRU;
} HotNameEntry;

HotNameEntry *FindMatchInHotNameCache(char *matchName);

void EnterIntoHotNameCache(HotNameEntry **temp);

void EnterIntoHotCGICache(HotNameEntry **temp);

struct CGIInfo;
HotNameEntry *FindMatchInHotCGICache(struct CGIInfo *cg, char *matchName);

HotNameEntry *GetEmptyHNE(void);

void ReleaseHNE(HotNameEntry **entry);

void DumpHotNameStats(void);
