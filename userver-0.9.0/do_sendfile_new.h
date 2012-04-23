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

#ifndef DO_SENDFILE_NEW_H
#define DO_SENDFILE_NEW_H

#include "info.h"

void process_sendfile_errors(int sd, int err);
/* Check if we need to make the socket blocking and do so if needed */
int blocking_sendfile_on(int sd);
/* Check if we need to make the socket nonblocking again and do so if needed */
void blocking_sendfile_off(int sd, int oldflags);
void fcntl_fail(char *err);

#if defined(__linux__) || defined(SUNOS)
int sendfile_linux(struct info *ip, int hdr_len, int file_len, int file_fd);
#elif defined(FreeBSD)
int sendfile_freebsd(struct info *ip, int hdr_len, int file_len, int file_fd);
#elif defined(HPUX)
int sendfile_hpux(struct info *ip, int hdr_len, int file_len, int file_fd);
#else
#error sendfile not implemented on this architecture
#endif


#endif /* DO_SENDFILE_NEW_H */
#endif /* HAVE_SENDFILE */
