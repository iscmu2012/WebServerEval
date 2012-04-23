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

#ifndef RANGE_REQUESTS_H

#include "info.h"

/* Determine if a request is for a range of bytes or not */
int range_request(struct info *ip);

/* Determine the length (number of bytes) in a range request */
off_t range_len(struct info *ip);

int do_range_header(struct info *ip, int version, size_t filesize,
  char *type);

#define MIN(a,b)               ( ((a) < (b)) ? (a) : (b) )

#endif /* RANGE_REQUESTS_H */
