/* wc - print the number of bytes, words, and lines in files
   Copyright (C) 85, 91, 95, 1996 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Paul Rubin, phr@ocf.berkeley.edu
   and David MacKenzie, djm@gnu.ai.mit.edu. */

#include <config.h>

#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include "system.h"
#include "error.h"

/* Size of atomic reads. */
#define BUFFER_SIZE (16 * 1024)

int safe_read ();

/* The name this program was run with. */
char *program_name;

/* Cumulative number of lines, words, and chars in all files so far. */
static unsigned long total_lines, total_words, total_chars;

/* Which counts to print. */
static int print_lines, print_words, print_chars;

/* Nonzero if we have ever read the standard input. */
static int have_read_stdin;

/* The error code to return to the system. */
static int exit_status;

/* If nonzero, display usage information and exit.  */
static int show_help;

/* If nonzero, print the version on standard output then exits.  */
static int show_version;

static struct option const longopts[] =
{
  {"bytes", no_argument, NULL, 'c'},
  {"chars", no_argument, NULL, 'c'},
  {"lines", no_argument, NULL, 'l'},
  {"words", no_argument, NULL, 'w'},
  {"help", no_argument, &show_help, 1},
  {"version", no_argument, &show_version, 1},
  {NULL, 0, NULL, 0}
};

static void
usage (int status)
{
  if (status != 0)
    fprintf (stderr, _("Try `%s --help' for more information.\n"),
	     program_name);
  else
    {
      printf (_("\
Usage: %s [OPTION]... [FILE]...\n\
"),
	      program_name);
      printf (_("\
Print line, word, and byte counts for each FILE, and a total line if\n\
more than one FILE is specified.  With no FILE, or when FILE is -,\n\
read standard input.\n\
  -c, --bytes, --chars   print the byte counts\n\
  -l, --lines            print the newline counts\n\
  -w, --words            print the word counts\n\
      --help             display this help and exit\n\
      --version          output version information and exit\n\
"));
      puts (_("\nReport bugs to textutils-bugs@gnu.ai.mit.edu"));
    }
  exit (status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void
write_counts (long unsigned int lines, long unsigned int words,
	      long unsigned int chars, const char *file)
{
  if (print_lines)
    printf ("%7lu", lines);
  if (print_words)
    {
      if (print_lines)
	putchar (' ');
      printf ("%7lu", words);
    }
  if (print_chars)
    {
      if (print_lines || print_words)
	putchar (' ');
      printf ("%7lu", chars);
    }
  if (*file)
    printf (" %s", file);
  putchar ('\n');
}

static void
wc (int fd, const char *file)
{
  char buf[BUFFER_SIZE + 1];
  register int bytes_read;
  register int in_word = 0;
  register unsigned long lines, words, chars;

  lines = words = chars = 0;

  /* When counting only bytes, save some line- and word-counting
     overhead.  If FD is a `regular' Unix file, using lseek is enough
     to get its `size' in bytes.  Otherwise, read blocks of BUFFER_SIZE
     bytes at a time until EOF.  Note that the `size' (number of bytes)
     that wc reports is smaller than stats.st_size when the file is not
     positioned at its beginning.  That's why the lseek calls below are
     necessary.  For example the command
     `(dd ibs=99k skip=1 count=0; ./wc -c) < /etc/group'
     should make wc report `0' bytes.  */

  if (print_chars && !print_words && !print_lines)
    {
      off_t current_pos, end_pos;
      struct stat stats;

      if (fstat (fd, &stats) == 0 && S_ISREG (stats.st_mode)
	  && (current_pos = lseek (fd, (off_t) 0, SEEK_CUR)) != -1
	  && (end_pos = lseek (fd, (off_t) 0, SEEK_END)) != -1)
	{
	  off_t diff;
	  /* Be careful here.  The current position may actually be
	     beyond the end of the file.  As in the example above.  */
	  chars = (diff = end_pos - current_pos) < 0 ? 0 : diff;
	}
      else
	{
	  while ((bytes_read = safe_read (fd, buf, BUFFER_SIZE)) > 0)
	    {
	      chars += bytes_read;
	    }
	  if (bytes_read < 0)
	    {
	      error (0, errno, "%s", file);
	      exit_status = 1;
	    }
	}
    }
  else if (!print_words)
    {
      /* Use a separate loop when counting only lines or lines and bytes --
	 but not words.  */
      while ((bytes_read = safe_read (fd, buf, BUFFER_SIZE)) > 0)
	{
	  register char *p = buf;

	  while ((p = memchr (p, '\n', (buf + bytes_read) - p)))
	    {
	      ++p;
	      ++lines;
	    }
	  chars += bytes_read;
	}
      if (bytes_read < 0)
	{
	  error (0, errno, "%s", file);
	  exit_status = 1;
	}
    }
  else
    {
      while ((bytes_read = safe_read (fd, buf, BUFFER_SIZE)) > 0)
	{
	  register char *p = buf;

	  chars += bytes_read;
	  do
	    {
	      switch (*p++)
		{
		case '\n':
		  lines++;
		  /* Fall through. */
		case '\r':
		case '\f':
		case '\t':
		case '\v':
		case ' ':
		  if (in_word)
		    {
		      in_word = 0;
		      words++;
		    }
		  break;
		default:
		  in_word = 1;
		  break;
		}
	    }
	  while (--bytes_read);
	}
      if (bytes_read < 0)
	{
	  error (0, errno, "%s", file);
	  exit_status = 1;
	}
      if (in_word)
	words++;
    }

  write_counts (lines, words, chars, file);
  total_lines += lines;
  total_words += words;
  total_chars += chars;
}

static void
wc_file (const char *file)
{
  if (!strcmp (file, "-"))
    {
      have_read_stdin = 1;
      wc (0, file);
    }
  else
    {
      int fd = open (file, O_RDONLY);
      if (fd == -1)
	{
	  error (0, errno, "%s", file);
	  exit_status = 1;
	  return;
	}
      wc (fd, file);
      if (close (fd))
	{
	  error (0, errno, "%s", file);
	  exit_status = 1;
	}
    }
}

int
main (int argc, char **argv)
{
  int optc;
  int nfiles;

  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  exit_status = 0;
  print_lines = print_words = print_chars = 0;
  total_lines = total_words = total_chars = 0;

  while ((optc = getopt_long (argc, argv, "clw", longopts, NULL)) != -1)
    switch (optc)
      {
      case 0:
	break;

      case 'c':
	print_chars = 1;
	break;

      case 'l':
	print_lines = 1;
	break;

      case 'w':
	print_words = 1;
	break;

      default:
	usage (1);
      }

  if (show_version)
    {
      printf ("wc (%s) %s\n", GNU_PACKAGE, VERSION);
      exit (EXIT_SUCCESS);
    }

  if (show_help)
    usage (0);

  if (print_lines + print_words + print_chars == 0)
    print_lines = print_words = print_chars = 1;

  nfiles = argc - optind;

  if (nfiles == 0)
    {
      have_read_stdin = 1;
      wc (0, "");
    }
  else
    {
      for (; optind < argc; ++optind)
	wc_file (argv[optind]);

      if (nfiles > 1)
	write_counts (total_lines, total_words, total_chars, _("total"));
    }

  if (have_read_stdin && close (0))
    error (EXIT_FAILURE, errno, "-");

  exit (exit_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
