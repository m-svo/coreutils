/* An interface to read that retries after interrupts.
   Copyright (C) 1993, 1994, 1998, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

/* Specification.  */
#include "safe-read.h"

/* Get ssize_t.  */
#include <sys/types.h>
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <errno.h>
#ifndef errno
extern int errno;
#endif

#include <limits.h>

#ifndef CHAR_BIT
# define CHAR_BIT 8
#endif

/* The extra casts work around common compiler bugs.  */
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))
/* The outer cast is needed to work around a bug in Cray C 5.0.3.0.
   It is necessary at least when t == time_t.  */
#define TYPE_MINIMUM(t) ((t) (TYPE_SIGNED (t) \
			      ? ~ (t) 0 << (sizeof (t) * CHAR_BIT - 1) : (t) 0))
#define TYPE_MAXIMUM(t) ((t) (~ (t) 0 - TYPE_MINIMUM (t)))

#ifndef INT_MAX
# define INT_MAX TYPE_MAXIMUM (int)
#endif

/* We don't pass an nbytes count > SSIZE_MAX to read() - POSIX says the
   effect would be implementation-defined.  Also we don't pass an nbytes
   count > INT_MAX but <= SSIZE_MAX to read() - this triggers a bug in
   Tru64 5.1.  */
#define MAX_BYTES_TO_READ INT_MAX

#ifndef EINTR
/* If a system doesn't have support for EINTR, define it
   to be a value to which errno will never be set.  */
# define EINTR (INT_MAX - 10)
#endif

/* Read up to COUNT bytes at BUF from descriptor FD, retrying if interrupted.
   Return the actual number of bytes read, zero for EOF, or SAFE_READ_ERROR
   upon error.  */
size_t
safe_read (int fd, void *buf, size_t count)
{
  size_t nbytes_to_read = count;
  ssize_t result;

  /* Limit the number of bytes to read, to avoid running
     into unspecified behaviour.  But keep the file pointer block
     aligned when doing so.  */
  if (nbytes_to_read > MAX_BYTES_TO_READ)
    nbytes_to_read = MAX_BYTES_TO_READ & ~8191;

  do
    {
      result = read (fd, buf, nbytes_to_read);
    }
  while (result < 0 && errno == EINTR);

  return (size_t) result;
}
