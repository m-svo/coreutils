/* fold -- wrap each input line to fit in specified width.
   Copyright (C) 91, 95, 1996 Free Software Foundation, Inc.

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

/* Written by David MacKenzie, djm@gnu.ai.mit.edu. */

#include <config.h>

/* Get isblank from GNU libc.  */
#define _GNU_SOURCE

#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>

#if HAVE_LIMITS_H
# include <limits.h>
#endif

#ifndef UINT_MAX
# define UINT_MAX ((unsigned int) ~(unsigned int) 0)
#endif

#ifndef INT_MAX
# define INT_MAX ((int) (UINT_MAX >> 1))
#endif

#include "system.h"
#include "xstrtol.h"
#include "error.h"

char *xrealloc ();
char *xmalloc ();

/* The name this program was run with. */
char *program_name;

/* If nonzero, try to break on whitespace. */
static int break_spaces;

/* If nonzero, count bytes, not column positions. */
static int count_bytes;

/* If nonzero, at least one of the files we read was standard input. */
static int have_read_stdin;

/* If nonzero, display usage information and exit.  */
static int show_help;

/* If nonzero, print the version on standard output then exit.  */
static int show_version;

static struct option const longopts[] =
{
  {"bytes", no_argument, NULL, 'b'},
  {"spaces", no_argument, NULL, 's'},
  {"width", required_argument, NULL, 'w'},
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
Wrap input lines in each FILE (standard input by default), writing to\n\
standard output.\n\
\n\
  -b, --bytes         count bytes rather than columns\n\
  -s, --spaces        break at spaces\n\
  -w, --width=WIDTH   use WIDTH columns instead of 80\n\
"));
      puts (_("\nReport bugs to textutils-bugs@gnu.ai.mit.edu"));
    }
  exit (status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

/* Assuming the current column is COLUMN, return the column that
   printing C will move the cursor to.
   The first column is 0. */

static int
adjust_column (int column, char c)
{
  if (!count_bytes)
    {
      if (c == '\b')
	{
	  if (column > 0)
	    column--;
	}
      else if (c == '\r')
	column = 0;
      else if (c == '\t')
	column = column + 8 - column % 8;
      else /* if (isprint (c)) */
	column++;
    }
  else
    column++;
  return column;
}

/* Fold file FILENAME, or standard input if FILENAME is "-",
   to stdout, with maximum line length WIDTH.
   Return 0 if successful, 1 if an error occurs. */

static int
fold_file (char *filename, int width)
{
  FILE *istream;
  register int c;
  int column = 0;		/* Screen column where next char will go. */
  int offset_out = 0;	/* Index in `line_out' for next char. */
  static char *line_out = NULL;
  static int allocated_out = 0;

  if (!strcmp (filename, "-"))
    {
      istream = stdin;
      have_read_stdin = 1;
    }
  else
    istream = fopen (filename, "r");

  if (istream == NULL)
    {
      error (0, errno, "%s", filename);
      return 1;
    }

  while ((c = getc (istream)) != EOF)
    {
      if (offset_out + 1 >= allocated_out)
	{
	  allocated_out += 1024;
	  line_out = xrealloc (line_out, allocated_out);
	}

      if (c == '\n')
	{
	  line_out[offset_out++] = c;
	  fwrite (line_out, sizeof (char), (size_t) offset_out, stdout);
	  column = offset_out = 0;
	  continue;
	}

    rescan:
      column = adjust_column (column, c);

      if (column > width)
	{
	  /* This character would make the line too long.
	     Print the line plus a newline, and make this character
	     start the next line. */
	  if (break_spaces)
	    {
	      /* Look for the last blank. */
	      int logical_end;

	      for (logical_end = offset_out - 1; logical_end >= 0;
		   logical_end--)
		if (ISBLANK (line_out[logical_end]))
		  break;
	      if (logical_end >= 0)
		{
		  int i;

		  /* Found a blank.  Don't output the part after it. */
		  logical_end++;
		  fwrite (line_out, sizeof (char), (size_t) logical_end,
			  stdout);
		  putchar ('\n');
		  /* Move the remainder to the beginning of the next line.
		     The areas being copied here might overlap. */
		  memmove (line_out, line_out + logical_end,
			   offset_out - logical_end);
		  offset_out -= logical_end;
		  for (column = i = 0; i < offset_out; i++)
		    column = adjust_column (column, line_out[i]);
		  goto rescan;
		}
	    }
	  else
	    {
	      if (offset_out == 0)
		{
		  line_out[offset_out++] = c;
		  continue;
		}
	    }
	  line_out[offset_out++] = '\n';
	  fwrite (line_out, sizeof (char), (size_t) offset_out, stdout);
	  column = offset_out = 0;
	  goto rescan;
	}

      line_out[offset_out++] = c;
    }

  if (offset_out)
    fwrite (line_out, sizeof (char), (size_t) offset_out, stdout);

  if (ferror (istream))
    {
      error (0, errno, "%s", filename);
      if (strcmp (filename, "-"))
	fclose (istream);
      return 1;
    }
  if (strcmp (filename, "-") && fclose (istream) == EOF)
    {
      error (0, errno, "%s", filename);
      return 1;
    }

  if (ferror (stdout))
    {
      error (0, errno, _("write error"));
      return 1;
    }

  return 0;
}

int
main (int argc, char **argv)
{
  int width = 80;
  int i;
  int optc;
  int errs = 0;

  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  break_spaces = count_bytes = have_read_stdin = 0;

  /* Turn any numeric options into -w options.  */
  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-' && ISDIGIT (argv[i][1]))
	{
	  char *s;

	  s = xmalloc (strlen (argv[i]) + 2);
	  s[0] = '-';
	  s[1] = 'w';
	  strcpy (s + 2, argv[i] + 1);
	  argv[i] = s;
	}
    }

  while ((optc = getopt_long (argc, argv, "bsw:", longopts, (int *) 0))
	 != EOF)
    {
      switch (optc)
	{
	case 0:
	  break;

	case 'b':		/* Count bytes rather than columns. */
	  count_bytes = 1;
	  break;

	case 's':		/* Break at word boundaries. */
	  break_spaces = 1;
	  break;

	case 'w':		/* Line width. */
	  {
	    long int tmp_long;
	    if (xstrtol (optarg, NULL, 10, &tmp_long, "") != LONGINT_OK
		|| tmp_long <= 0 || tmp_long > INT_MAX)
	      error (EXIT_FAILURE, 0,
		     _("invalid number of columns: `%s'"), optarg);
	    width = (int) tmp_long;
	  }
	  break;

	default:
	  usage (1);
	}
    }

  if (show_version)
    {
      printf ("fold (%s) %s\n", GNU_PACKAGE, VERSION);
      exit (EXIT_SUCCESS);
    }

  if (show_help)
    usage (0);

  if (argc == optind)
    errs |= fold_file ("-", width);
  else
    for (i = optind; i < argc; i++)
      errs |= fold_file (argv[i], width);

  if (have_read_stdin && fclose (stdin) == EOF)
    error (EXIT_FAILURE, errno, "-");
  if (fclose (stdout) == EOF)
    error (EXIT_FAILURE, errno, _("write error"));

  exit (errs == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
