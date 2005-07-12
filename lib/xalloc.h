/* xalloc.h -- malloc with out-of-memory checking

   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2003, 2004, 2005 Free Software Foundation, Inc.

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
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef XALLOC_H_
# define XALLOC_H_

# include <stddef.h>


# ifdef __cplusplus
extern "C" {
# endif


# ifndef __attribute__
#  if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8) || __STRICT_ANSI__
#   define __attribute__(x)
#  endif
# endif

# ifndef ATTRIBUTE_NORETURN
#  define ATTRIBUTE_NORETURN __attribute__ ((__noreturn__))
# endif

/* This function is always triggered when memory is exhausted.
   It must be defined by the application, either explicitly
   or by using gnulib's xalloc-die module.  This is the
   function to call when one wants the program to die because of a
   memory allocation failure.  */
extern void xalloc_die (void) ATTRIBUTE_NORETURN;

void *xmalloc (size_t s);
void *xnmalloc (size_t n, size_t s);
void *xzalloc (size_t s);
void *xcalloc (size_t n, size_t s);
void *xrealloc (void *p, size_t s);
void *xnrealloc (void *p, size_t n, size_t s);
void *x2realloc (void *p, size_t *pn);
void *x2nrealloc (void *p, size_t *pn, size_t s);
void *xmemdup (void const *p, size_t s);
char *xstrdup (char const *str);

/* Verify a requirement at compile-time (unlike assert, which is runtime).  */
# undef VERIFY_EXPR
# define VERIFY_EXPR(assertion) \
   (void)((struct {char a[(assertion) ? 1 : -1]; } *) 0)

/* This is simply a shorthand for the common case in which
   the third argument to x2nrealloc would be `sizeof (*P)'.
   Ensure that sizeof (*P) is *not* 1.  In that case, it'd be
   better to use X2REALLOC, although not strictly necessary.  */
# define X2NREALLOC(P, PN) (VERIFY_EXPR (sizeof(*P) != 1), \
                            x2nrealloc (P, PN, sizeof (*P)))

/* Using x2realloc (when appropriate) usually makes your code more
   readable than using x2nrealloc, but it also makes it so your
   code will malfunction if sizeof (*P) ever becomes 2 or greater.
   So use this macro instead of using x2realloc directly.  */
# define X2REALLOC(P, PN) (VERIFY_EXPR (sizeof(*P) == 1), x2realloc (P, PN))

/* Return 1 if an array of N objects, each of size S, cannot exist due
   to size arithmetic overflow.  S must be positive and N must be
   nonnegative.  This is a macro, not an inline function, so that it
   works correctly even when SIZE_MAX < N.

   By gnulib convention, SIZE_MAX represents overflow in size
   calculations, so the conservative dividend to use here is
   SIZE_MAX - 1, since SIZE_MAX might represent an overflowed value.
   However, malloc (SIZE_MAX) fails on all known hosts where
   sizeof (ptrdiff_t) <= sizeof (size_t), so do not bother to test for
   exactly-SIZE_MAX allocations on such hosts; this avoids a test and
   branch when S is known to be 1.  */
# define xalloc_oversized(n, s) \
    ((size_t) (sizeof (ptrdiff_t) <= sizeof (size_t) ? -1 : -2) / (s) < (n))

# ifdef __cplusplus
}
# endif


#endif /* !XALLOC_H_ */
