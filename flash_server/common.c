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


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <sys/param.h>
#include "common.h"

/* ---------------------------------------------------------------- */
void
PanicBack(char *name, int line, char *temp)
{
  fprintf(stderr, "file %s, line %d, %s\n", name, line, temp);
  exit(-1);
}
/* ---------------------------------------------------------------- */
char *
GetField(unsigned char *start, int whichField)
{
  int currField;

  /* move to first non-blank char */
  while (isspace(*start))
    start++;

  if (*start == '\0')
    return(NULL);

  for (currField = 0; currField < whichField; currField++) {
    /* move over this field */
    while (*start != '\0' && (!isspace(*start)))
      start++;
    /* move over blanks before next field */
    while (isspace(*start))
      start++;
    if (*start == '\0')
      return(NULL);
  }
  return((char *) start);
}
/* ---------------------------------------------------------------- */
char *
GetWord(unsigned char *start, int whichWord)
{
  /* returns a newly allocated string containing the desired word,
     or NULL if there was a problem */
  unsigned char *temp;
  int len = 0;
  char *res;

  temp = (unsigned char *) GetField(start, whichWord);
  if (!temp)
    return(NULL);
  while (!(temp[len] == '\0' || isspace(temp[len])))
    len++;
  if (!len)
    Panic("internal error");
  res = calloc(1, len+1);
  if (!res)
    Panic("failed allocating mem in GetWord");
  memcpy(res, temp, len);
  return(res);
}
/* ---------------------------------------------------------------- */
void
ScanOptions(int argc, char *argv[], int startArg, 
	    struct StringOptions *stringOptions)
{
  int i;
  int bad = 0;

  for (i = startArg; i < argc; i++) {
    int j;
    for (j = 0; stringOptions[j].so_name; j++) {
      if (strcmp(stringOptions[j].so_name, argv[i]) == 0) {
	*stringOptions[j].so_valuePointer = argv[i+1];
	if (i+1 >= argc) {
	  fprintf(stderr, "no value supplied for %s option\n", 
		  stringOptions[j].so_name);
	  bad = 1;
	  i = argc;
	  break;
	}
	i++; /* skip over value for string switch */
	break;
      }
    }
    if (!stringOptions[j].so_name) {
      fprintf(stderr, "bad option %s\n", argv[i]);
      bad = 1;
      break;
    }
  }

  for (i = 0; stringOptions[i].so_name; i++) {
    char *comment = stringOptions[i].so_comment;
    char *value = *stringOptions[i].so_valuePointer;
    if (!comment)
      comment = "";
    if (!value)
      value = "";
    printf("%s %s : %s\n", stringOptions[i].so_name, 
	   value, comment);
  }
  if (bad)
    exit(-1);
}
/* ---------------------------------------------------------------- */
int
ReadSummaryFile(char *prefixName, int maxFiles, int sizes[], int reqs[])
{
  /* 
     reads the summary file in question -
     if sizes[] is non-null, fills in the sizes of the files
     if reqs[] is non-null, fills in the # requests for the file
     maxFiles specifies the size of the arrays

     returns number of files in summary
     */

  char name[1024];
  char line[1024];
  FILE *dataFile;
  int i;
  int numFiles = 0;

#define SUM_FILE_FIELD 0
#define SUM_REQS_FIELD 1
#define SUM_SIZE_FIELD 2

  assert(sizes || reqs);
  assert(maxFiles > 0);

  if (DoesSuffixMatch(prefixName, ".summary") ||
      DoesSuffixMatch(prefixName, ".summary.gz"))
    strcpy(name, prefixName);
  else
    sprintf(name, "%s.summary", prefixName);
  dataFile = OpenAnyFile(name);
  if (!dataFile)
    Panic("datafile - summary");

  /* get information about files */
  while (fgets(line, sizeof(line), dataFile)) {
    char *fileNumPtr;
    char *sizePtr = NULL;
    char *reqsPtr = NULL;
    int whichFile;
    int fileSize = 0;
    int numReqs = 0;

    fileNumPtr = GetField((unsigned char *) line, SUM_FILE_FIELD);
    if (!fileNumPtr)
      Panic("couldn't get file num in summary");
    whichFile = atoi(fileNumPtr);
    if (whichFile < 0)
      Panic("negative file num");
 
    if (sizes) {
      sizePtr = GetField((unsigned char *) line, SUM_SIZE_FIELD);
      if (!sizePtr)
	Panic("couldn't get size value in summary");
      fileSize = atoi(sizePtr);
      if (fileSize < 1)
	Panic("non-positive size val in summary");
    }

    if (reqs) {
      reqsPtr = GetField((unsigned char *) line, SUM_REQS_FIELD);
      if (!reqsPtr)
	Panic("couldn't get reqs value in summary");
      numReqs = atoi(reqsPtr);
      if (numReqs < 1)
	Panic("non-positive num reqs");
    }
      
    if (whichFile >= maxFiles) {
      fprintf(stderr, 
	      "encountered file #%d in summary, MAXFILESINSUMMARY is %d\n",
	      whichFile, maxFiles);
      exit(-1);
    }
    
    if (sizes && sizes[whichFile]) {
      fprintf(stderr, "multiple data for file %d\n", whichFile);
      exit(-1);
    }

    if (reqs && reqs[whichFile]) {
      fprintf(stderr, "multiple data for file %d\n", whichFile);
      exit(-1);
    }

    if (sizes)
      sizes[whichFile] = fileSize;
    if (reqs)
      reqs[whichFile] = numReqs;
    numFiles++;
  }
  CloseAnyFile(dataFile);
  
  for (i = 0; i < numFiles; i++) {
    if (sizes && sizes[i])
      continue;
    if (reqs && reqs[i])
      continue;
    Panic("file number gaps in summary");
  }
  return(numFiles);
}
/* ---------------------------------------------------------------- */
static int numPopenFiles;
#define MAXPOPENFILES 20
static FILE *popenFiles[MAXPOPENFILES];
/* ---------------------------------------------------------------- */
static FILE *
OpenAnyFileBackend(char *filename)
{
  FILE *newFile;
  int nameLen;
  char *suf = ".gz";
  int sufLen;
  char commandLine[1024];

  /* test if file exists */
  newFile = fopen(filename, "r");
  if (!newFile)
    return(NULL);

  /* if it's not compressed, we're done */
  nameLen = strlen(filename);
  sufLen = strlen(suf);
  if (nameLen <= sufLen)
    return(newFile);
  if (strcmp(&filename[nameLen-sufLen], suf))
    return(newFile);

  /* otherwise, close and re-open with popen */
  fclose(newFile);
  sprintf(commandLine, "gunzip -c %s", filename);
  newFile = popen(commandLine, "r");
  if (!newFile)
    Panic("popen failed");

  if (numPopenFiles >= MAXPOPENFILES)
    Panic("too many popen files in common.c");

  popenFiles[numPopenFiles] = newFile;
  numPopenFiles++;
  return(newFile);
}
/* ---------------------------------------------------------------- */
FILE *
OpenAnyFile(char *filename)
{
  FILE *f;
  char buf[MAXPATHLEN];
  
  f = OpenAnyFileBackend(filename);
  if (f)
    return(f);
  sprintf(buf, "%s.gz", filename);
  return(OpenAnyFileBackend(buf));
}
/* ---------------------------------------------------------------- */
void
CloseAnyFile(FILE *f)
{
  int i;
  int pos = 0;
  int isPopen = FALSE;

  for (i = 0; i < numPopenFiles; i++)
    if (popenFiles[i] == f) {
      isPopen = TRUE;
      pos = i;
      break;
    }

  if (!isPopen) {
    fclose(f);
    return;
  }
  
  /* shift everything */
  for (i = pos+1; i < numPopenFiles; i++)
    popenFiles[i-1] = popenFiles[i];
  numPopenFiles--;
  pclose(f);
  return;
}
/* ---------------------------------------------------------------- */
FILE *
OpenTraceFile(char *prefixName)
{
  char name[1024];
  
  if (DoesSuffixMatch(prefixName, ".trace") ||
      DoesSuffixMatch(prefixName, ".trace.gz") ||
      DoesSuffixMatch(prefixName, ".randtrace") ||
      DoesSuffixMatch(prefixName, ".randtrace.gz"))
    return(OpenAnyFile(prefixName));

  sprintf(name, "%s.trace", prefixName);
  return(OpenAnyFile(name));
}
/* ---------------------------------------------------------------- */
void 
CloseTraceFile(FILE *trace)
{
  CloseAnyFile(trace);
}
/* ---------------------------------------------------------------- */
int
StripSuffix(char *string, char *suffix)
{
  /* if suffix matches, removes suffix and terminates
     string with a nul at that point.
     returns nonzero if suffix matched and was stripped */
  int strLen;
  int sufLen;

  if (!DoesSuffixMatch(string, suffix))
    return(0);
  strLen = strlen(string);
  sufLen = strlen(suffix);
  string[strLen - sufLen] = 0;
  return(1);
}
/* ---------------------------------------------------------------- */
int
DoesSuffixMatch(char *string, char *suffix) 
{
  /* returns nonzero if end of string matches suffix, and
     there's some other part to the string than the suffix */
  int sufLen;
  int strLen;

  sufLen = strlen(suffix);
  strLen = strlen(string);
  if (sufLen >= strLen)
    return(0);
  return(!strcmp(&string[strLen-sufLen], suffix));
}
/* ---------------------------------------------------------------- */

