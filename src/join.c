/* join - join lines of two files on a common field
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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

   Written by Mike Haertel, mike@gnu.ai.mit.edu.  */

#include <config.h>

#ifdef __GNUC__
#define alloca __builtin_alloca
#else /* not __GNUC__ */
#if HAVE_ALLOCA_H
#include <alloca.h>
#else /* not HAVE_ALLOCA_H */
#ifdef _AIX
 #pragma alloca
#else /* not _AIX */
char *alloca ();
#endif /* not _AIX */
#endif /* not HAVE_ALLOCA_H */
#endif /* not __GNUC__ */

/* Get isblank from GNU libc.  */
#define _GNU_SOURCE

#include <stdio.h>
#define NDEBUG
#include <assert.h>
#include <sys/types.h>
#include <getopt.h>

#if HAVE_LIMITS_H
# include <limits.h>
#endif

#ifndef UINT_MAX
# define UINT_MAX ((unsigned int) ~(unsigned int) 0)
#endif

#ifndef INT_MAX
# define INT_MAX ((int) (UINT_MAX >> 1))
#endif

#if _LIBC || STDC_HEADERS
# define TOLOWER(c) tolower (c)
#else
# define TOLOWER(c) (ISUPPER (c) ? tolower (c) : (c))
#endif

#include "system.h"
#include "long-options.h"
#include "xstrtol.h"
#include "error.h"
#include "memcasecmp.h"

#define join system_join

char *xmalloc ();
char *xrealloc ();

/* Undefine, to avoid warning about redefinition on some systems.  */
#undef min
#undef max
#define min(A, B) ((A) < (B) ? (A) : (B))
#define max(A, B) ((A) > (B) ? (A) : (B))

/* An element of the list identifying which fields to print for each
   output line.  */
struct outlist
  {
    /* File number: 0, 1, or 2.  0 means use the join field.
       1 means use the first file argument, 2 the second.  */
    int file;

    /* Field index (zero-based), specified only when FILE is 1 or 2.  */
    int field;

    struct outlist *next;
  };

/* A field of a line.  */
struct field
  {
    const char *beg;		/* First character in field.  */
    size_t len;			/* The length of the field.  */
  };

/* A line read from an input file.  Newlines are not stored.  */
struct line
  {
    char *beg;			/* First character in line.  */
    char *lim;			/* Character after last character in line.  */
    int nfields;		/* Number of elements in `fields'.  */
    int nfields_allocated;	/* Number of elements in `fields'.  */
    struct field *fields;
  };

/* One or more consecutive lines read from a file that all have the
   same join field value.  */
struct seq
  {
    int count;			/* Elements used in `lines'.  */
    int alloc;			/* Elements allocated in `lines'.  */
    struct line *lines;
  };

/* The name this program was run with.  */
char *program_name;

/* If nonzero, print unpairable lines in file 1 or 2.  */
static int print_unpairables_1, print_unpairables_2;

/* If nonzero, print pairable lines.  */
static int print_pairables;

/* Empty output field filler.  */
static char *empty_filler;

/* Field to join on.  */
static int join_field_1, join_field_2;

/* List of fields to print.  */
static struct outlist outlist_head;

/* Last element in `outlist', where a new element can be added.  */
static struct outlist *outlist_end = &outlist_head;

/* Tab character separating fields; if this is NUL fields are separated
   by any nonempty string of white space, otherwise by exactly one
   tab character.  */
static char tab;

/* When using getopt_long_only, no long option can start with
   a character that is a short option.  */
static struct option const longopts[] =
{
  {"ignore-case", no_argument, NULL, 'i'},
  {"j", required_argument, NULL, 'j'},
  {"j1", required_argument, NULL, '1'},
  {"j2", required_argument, NULL, '2'},
  {NULL, 0, NULL, 0}
};

/* Used to print non-joining lines */
static struct line uni_blank;

/* If nonzero, ignore case when comparing join fields.  */
static int ignore_case;

static void
usage (int status)
{
  if (status != 0)
    fprintf (stderr, _("Try `%s --help' for more information.\n"),
	     program_name);
  else
    {
      printf (_("\
Usage: %s [OPTION]... FILE1 FILE2\n\
"),
	      program_name);
      printf (_("\
For each pair of input lines with identical join fields, write a line to\n\
standard output.  The default join field is the first, delimited\n\
by whitespace.  When FILE1 or FILE2 (not both) is -, read standard input.\n\
\n\
  -a SIDE          print unpairable lines coming from file SIDE\n\
  -e EMPTY         replace missing input fields with EMPTY\n\
  -i, --ignore-case ignore differences in case when comparing fields\n\
  -j FIELD         (Obsolescent) equivalent to `-1 FIELD -2 FIELD'\n\
  -j1 FIELD        (Obsolescent) equivalent to `-1 FIELD'\n\
  -j2 FIELD        (Obsolescent) equivalent to `-2 FIELD'\n\
  -1 FIELD         join on this FIELD of file 1\n\
  -2 FIELD         join on this FIELD of file 2\n\
  -o FORMAT        obey FORMAT while constructing output line\n\
  -t CHAR          use CHAR as input and output field separator\n\
  -v SIDE          like -a SIDE, but suppress joined output lines\n\
  --help           display this help and exit\n\
  --version        output version information and exit\n\
\n\
Unless -t CHAR is given, leading blanks separate fields and are ignored,\n\
else fields are separated by CHAR.  Any FIELD is a field number counted\n\
from 1.  FORMAT is one or more comma or blank separated specifications,\n\
each being `SIDE.FIELD' or `0'.  Default FORMAT outputs the join field,\n\
the remaining fields from FILE1, the remaining fields from FILE2, all\n\
separated by CHAR.\n\
"));
    }
  exit (status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void
ADD_FIELD (struct line *line, const char *field, size_t len)
{
  if (line->nfields >= line->nfields_allocated)
    {
      line->nfields_allocated = (3 * line->nfields_allocated) / 2 + 1;
      line->fields = (struct field *) xrealloc ((char *) line->fields,
						(line->nfields_allocated
						 * sizeof (struct field)));
    }
  line->fields[line->nfields].beg = field;
  line->fields[line->nfields].len = len;
  ++(line->nfields);
}

/* Fill in the `fields' structure in LINE.  */

static void
xfields (struct line *line)
{
  int i;
  register char *ptr, *lim;

  ptr = line->beg;
  lim = line->lim;

  if (!tab)
    {
      /* Skip leading blanks before the first field.  */
      while (ptr < lim && ISSPACE (*ptr))
	++ptr;
    }

  for (i = 0; ptr < lim; ++i)
    {
      if (tab)
	{
	  char *beg;

	  beg = ptr;
	  while (ptr < lim && *ptr != tab)
	    ++ptr;
	  ADD_FIELD (line, beg, ptr - beg);
	  if (ptr < lim)
	    ++ptr;
	}
      else
	{
	  char *beg;

	  beg = ptr;
	  while (ptr < lim && !ISSPACE (*ptr))
	    ++ptr;
	  ADD_FIELD (line, beg, ptr - beg);
	  while (ptr < lim && ISSPACE (*ptr))
	    ++ptr;
	}
    }

  if (ptr > line->beg && ((tab && ISSPACE (ptr[-1])) || ptr[-1] == tab))
    {
      /* Add one more (empty) field because the last character of the
	 line was a delimiter.  */
      ADD_FIELD (line, NULL, 0);
    }
}

/* Read a line from FP into LINE and split it into fields.
   Return 0 if EOF, 1 otherwise.  */

static int
get_line (FILE *fp, struct line *line)
{
  static int linesize = 80;
  int c, i;
  char *ptr;

  if (feof (fp))
    return 0;

  ptr = xmalloc (linesize);

  for (i = 0; (c = getc (fp)) != EOF && c != '\n'; ++i)
    {
      if (i == linesize)
	{
	  linesize *= 2;
	  ptr = xrealloc (ptr, linesize);
	}
      ptr[i] = c;
    }

  if (c == EOF && i == 0)
    {
      free (ptr);
      return 0;
    }

  line->beg = ptr;
  line->lim = line->beg + i;
  line->nfields_allocated = 0;
  line->nfields = 0;
  line->fields = NULL;
  xfields (line);
  return 1;
}

static void
freeline (struct line *line)
{
  free ((char *) line->fields);
  free (line->beg);
  line->beg = NULL;
}

static void
initseq (struct seq *seq)
{
  seq->count = 0;
  seq->alloc = 1;
  seq->lines = (struct line *) xmalloc (seq->alloc * sizeof (struct line));
}

/* Read a line from FP and add it to SEQ.  Return 0 if EOF, 1 otherwise.  */

static int
getseq (FILE *fp, struct seq *seq)
{
  if (seq->count == seq->alloc)
    {
      seq->alloc *= 2;
      seq->lines = (struct line *)
	xrealloc ((char *) seq->lines, seq->alloc * sizeof (struct line));
    }

  if (get_line (fp, &seq->lines[seq->count]))
    {
      ++seq->count;
      return 1;
    }
  return 0;
}

static void
delseq (struct seq *seq)
{
  int i;
  for (i = 0; i < seq->count; i++)
    if (seq->lines[i].beg)
      freeline (&seq->lines[i]);
  free ((char *) seq->lines);
}

/* Return <0 if the join field in LINE1 compares less than the one in LINE2;
   >0 if it compares greater; 0 if it compares equal.  */

static int
keycmp (struct line *line1, struct line *line2)
{
  const char *beg1, *beg2;	/* Start of field to compare in each file.  */
  int len1, len2;		/* Length of fields to compare.  */
  int diff;

  if (join_field_1 < line1->nfields)
    {
      beg1 = line1->fields[join_field_1].beg;
      len1 = line1->fields[join_field_1].len;
    }
  else
    {
      beg1 = NULL;
      len1 = 0;
    }

  if (join_field_2 < line2->nfields)
    {
      beg2 = line2->fields[join_field_2].beg;
      len2 = line2->fields[join_field_2].len;
    }
  else
    {
      beg2 = NULL;
      len2 = 0;
    }

  if (len1 == 0)
    return len2 == 0 ? 0 : -1;
  if (len2 == 0)
    return 1;

  /* Use an if-statement here rather than a function variable to
     avoid portability hassles of getting a non-conflicting declaration
     of memcmp.  */
  if (ignore_case)
    diff = memcasecmp (beg1, beg2, min (len1, len2));
  else
    diff = memcmp (beg1, beg2, min (len1, len2));

  if (diff)
    return diff;
  return len1 - len2;
}

/* Print field N of LINE if it exists and is nonempty, otherwise
   `empty_filler' if it is nonempty.  */

static void
prfield (int n, struct line *line)
{
  int len;

  if (n < line->nfields)
    {
      len = line->fields[n].len;
      if (len)
	fwrite (line->fields[n].beg, 1, len, stdout);
      else if (empty_filler)
	fputs (empty_filler, stdout);
    }
  else if (empty_filler)
    fputs (empty_filler, stdout);
}

/* Print the join of LINE1 and LINE2.  */

static void
prjoin (struct line *line1, struct line *line2)
{
  const struct outlist *outlist;

  outlist = outlist_head.next;
  if (outlist)
    {
      const struct outlist *o;

      o = outlist;
      while (1)
	{
	  int field;
	  struct line *line;

	  if (o->file == 0)
	    {
	      if (line1 == &uni_blank)
	        {
		  line = line2;
		  field = join_field_2;
		}
	      else
	        {
		  line = line1;
		  field = join_field_1;
		}
	    }
	  else
	    {
	      line = (o->file == 1 ? line1 : line2);
	      field = o->field;
	    }
	  prfield (field, line);
	  o = o->next;
	  if (o == NULL)
	    break;
	  putchar (tab ? tab : ' ');
	}
      putchar ('\n');
    }
  else
    {
      int i;

      if (line1 == &uni_blank)
	{
	  struct line *t;
	  t = line1;
	  line1 = line2;
	  line2 = t;
	}
      prfield (join_field_1, line1);
      for (i = 0; i < join_field_1 && i < line1->nfields; ++i)
	{
	  putchar (tab ? tab : ' ');
	  prfield (i, line1);
	}
      for (i = join_field_1 + 1; i < line1->nfields; ++i)
	{
	  putchar (tab ? tab : ' ');
	  prfield (i, line1);
	}

      for (i = 0; i < join_field_2 && i < line2->nfields; ++i)
	{
	  putchar (tab ? tab : ' ');
	  prfield (i, line2);
	}
      for (i = join_field_2 + 1; i < line2->nfields; ++i)
	{
	  putchar (tab ? tab : ' ');
	  prfield (i, line2);
	}
      putchar ('\n');
    }
}

/* Print the join of the files in FP1 and FP2.  */

static void
join (FILE *fp1, FILE *fp2)
{
  struct seq seq1, seq2;
  struct line line;
  int diff, i, j, eof1, eof2;

  /* Read the first line of each file.  */
  initseq (&seq1);
  getseq (fp1, &seq1);
  initseq (&seq2);
  getseq (fp2, &seq2);

  while (seq1.count && seq2.count)
    {
      diff = keycmp (&seq1.lines[0], &seq2.lines[0]);
      if (diff < 0)
	{
	  if (print_unpairables_1)
	    prjoin (&seq1.lines[0], &uni_blank);
	  freeline (&seq1.lines[0]);
	  seq1.count = 0;
	  getseq (fp1, &seq1);
	  continue;
	}
      if (diff > 0)
	{
	  if (print_unpairables_2)
	    prjoin (&uni_blank, &seq2.lines[0]);
	  freeline (&seq2.lines[0]);
	  seq2.count = 0;
	  getseq (fp2, &seq2);
	  continue;
	}

      /* Keep reading lines from file1 as long as they continue to
         match the current line from file2.  */
      eof1 = 0;
      do
	if (!getseq (fp1, &seq1))
	  {
	    eof1 = 1;
	    ++seq1.count;
	    break;
	  }
      while (!keycmp (&seq1.lines[seq1.count - 1], &seq2.lines[0]));

      /* Keep reading lines from file2 as long as they continue to
         match the current line from file1.  */
      eof2 = 0;
      do
	if (!getseq (fp2, &seq2))
	  {
	    eof2 = 1;
	    ++seq2.count;
	    break;
	  }
      while (!keycmp (&seq1.lines[0], &seq2.lines[seq2.count - 1]));

      if (print_pairables)
	{
	  for (i = 0; i < seq1.count - 1; ++i)
	    for (j = 0; j < seq2.count - 1; ++j)
	      prjoin (&seq1.lines[i], &seq2.lines[j]);
	}

      for (i = 0; i < seq1.count - 1; ++i)
	freeline (&seq1.lines[i]);
      if (!eof1)
	{
	  seq1.lines[0] = seq1.lines[seq1.count - 1];
	  seq1.count = 1;
	}
      else
	seq1.count = 0;

      for (i = 0; i < seq2.count - 1; ++i)
	freeline (&seq2.lines[i]);
      if (!eof2)
	{
	  seq2.lines[0] = seq2.lines[seq2.count - 1];
	  seq2.count = 1;
	}
      else
	seq2.count = 0;
    }

  if (print_unpairables_1 && seq1.count)
    {
      prjoin (&seq1.lines[0], &uni_blank);
      freeline (&seq1.lines[0]);
      while (get_line (fp1, &line))
	{
	  prjoin (&line, &uni_blank);
	  freeline (&line);
	}
    }

  if (print_unpairables_2 && seq2.count)
    {
      prjoin (&uni_blank, &seq2.lines[0]);
      freeline (&seq2.lines[0]);
      while (get_line (fp2, &line))
	{
	  prjoin (&uni_blank, &line);
	  freeline (&line);
	}
    }

  delseq (&seq1);
  delseq (&seq2);
}

/* Add a field spec for field FIELD of file FILE to `outlist'.  */

static void
add_field (int file, int field)
{
  struct outlist *o;

  assert (file == 0 || file == 1 || file == 2);
  assert (field >= 0);

  o = (struct outlist *) xmalloc (sizeof (struct outlist));
  o->file = file;
  o->field = field;
  o->next = NULL;

  /* Add to the end of the list so the fields are in the right order.  */
  outlist_end->next = o;
  outlist_end = o;
}

/* Convert a single field specifier string, S, to a *FILE_INDEX, *FIELD_INDEX
   pair.  In S, the field index string is 1-based; *FIELD_INDEX is zero-based.
   If S is valid, return zero.  Otherwise, give a diagnostic, don't update
   *FILE_INDEX or *FIELD_INDEX, and return nonzero.  */

static int
decode_field_spec (const char *s, int *file_index, int *field_index)
{
  int invalid = 1;

  /* The first character must be 0, 1, or 2.  */
  switch (s[0])
    {
    case '0':
      if (s[1] == '\0')
	{
	  *file_index = 0;
	  /* Leave *field_index undefined.  */
	  invalid = 0;
	}
      else
        {
	  /* `0' must be all alone -- no `.FIELD'.  */
	  error (0, 0, _("invalid field specifier: `%s'"), s);
	}
      break;

    case '1':
    case '2':
      if (s[1] == '.' && s[2] != '\0')
        {
	  strtol_error s_err;
	  long int tmp_long;

	  s_err = xstrtol (s + 2, NULL, 10, &tmp_long, NULL);
	  if (s_err != LONGINT_OK || tmp_long <= 0 || tmp_long > INT_MAX)
	    {
	      error (0, 0, _("invalid field number: `%s'"), s + 2);
	    }
	  else
	    {
	      *file_index = s[0] - '0';
	      /* Convert to a zero-based index.  */
	      *field_index = (int) tmp_long - 1;
	      invalid = 0;
	    }
	}
      break;

    default:
      error (0, 0, _("invalid file number in field spec: `%s'"), s);
      break;
    }
  return invalid;
}

/* Add the comma or blank separated field spec(s) in STR to `outlist'.
   Return nonzero to indicate failure.  */

static int
add_field_list (const char *c_str)
{
  char *p, *str;

  /* Make a writable copy of c_str.  */
  str = (char *) alloca (strlen (c_str) + 1);
  strcpy (str, c_str);

  p = str;
  do
    {
      int invalid;
      int file_index, field_index;
      char *spec_item = p;

      p = strpbrk (p, ", \t");
      if (p)
        *p++ = 0;
      invalid = decode_field_spec (spec_item, &file_index, &field_index);
      if (invalid)
	return 1;
      add_field (file_index, field_index);
      uni_blank.nfields = max (uni_blank.nfields, field_index);
    }
  while (p);
  return 0;
}

/* Create a blank line with COUNT fields separated by tabs.  */

void
make_blank (struct line *blank, int count)
{
  int i;
  blank->nfields = count;
  blank->beg = xmalloc (blank->nfields + 1);
  blank->fields = (struct field *) xmalloc (sizeof (struct field) * count);
  for (i = 0; i < blank->nfields; i++)
    {
      blank->beg[i] = '\t';
      blank->fields[i].beg = &blank->beg[i];
      blank->fields[i].len = 0;
    }
  blank->beg[i] = '\0';
  blank->lim = &blank->beg[i];
}

int
main (int argc, char **argv)
{
  char *names[2];
  FILE *fp1, *fp2;
  int optc, prev_optc = 0, nfiles;

  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Initialize this before parsing options.  In parsing options,
     it may be increased.  */
  uni_blank.nfields = 1;

  parse_long_options (argc, argv, "join", PACKAGE_VERSION, usage);

  nfiles = 0;
  print_pairables = 1;

  while ((optc = getopt_long_only (argc, argv, "-a:e:i1:2:o:t:v:", longopts,
				   (int *) 0)) != EOF)
    {
      long int val;

      switch (optc)
	{
	case 0:
	  break;

	case 'v':
	    print_pairables = 0;
	    /* Fall through.  */

	case 'a':
	  if (xstrtol (optarg, NULL, 10, &val, NULL) != LONGINT_OK
	      || (val != 1 && val != 2))
	    error (EXIT_FAILURE, 0, _("invalid field number: `%s'"), optarg);
	  if (val == 1)
	    print_unpairables_1 = 1;
	  else
	    print_unpairables_2 = 1;
	  break;

	case 'e':
	  empty_filler = optarg;
	  break;

	case 'i':
	  ignore_case = 1;
	  break;

	case '1':
	  if (xstrtol (optarg, NULL, 10, &val, NULL) != LONGINT_OK
	      || val <= 0 || val > INT_MAX)
	    {
	      error (EXIT_FAILURE, 0,
		     _("invalid field number for file 1: `%s'"), optarg);
	    }
	  join_field_1 = (int) val - 1;
	  break;

	case '2':
	  if (xstrtol (optarg, NULL, 10, &val, NULL) != LONGINT_OK
	      || val <= 0 || val > INT_MAX)
	    error (EXIT_FAILURE, 0,
		   _("invalid field number for file 2: `%s'"), optarg);
	  join_field_2 = (int) val - 1;
	  break;

	case 'j':
	  if (xstrtol (optarg, NULL, 10, &val, NULL) != LONGINT_OK
	      || val <= 0 || val > INT_MAX)
	    error (EXIT_FAILURE, 0, _("invalid field number: `%s'"), optarg);
	  join_field_1 = join_field_2 = (int) val - 1;
	  break;

	case 'o':
	  if (add_field_list (optarg))
	    exit (EXIT_FAILURE);
	  break;

	case 't':
	  tab = *optarg;
	  break;

	case 1:		/* Non-option argument.  */
	  if (prev_optc == 'o' && optind <= argc - 2)
	    {
	      if (add_field_list (optarg))
		exit (EXIT_FAILURE);

	      /* Might be continuation of args to -o.  */
	      continue;		/* Don't change `prev_optc'.  */
	    }

	  if (nfiles > 1)
	    {
	      error (0, 0, _("too many non-option arguments"));
	      usage (1);
	    }
	  names[nfiles++] = optarg;
	  break;

	case '?':
	  usage (1);
	}
      prev_optc = optc;
    }

  /* Now that we've seen the options, we can construct the blank line
     structure.  */
  make_blank (&uni_blank, uni_blank.nfields);

  if (nfiles != 2)
    {
      error (0, 0, _("too few non-option arguments"));
      usage (1);
    }

  fp1 = strcmp (names[0], "-") ? fopen (names[0], "r") : stdin;
  if (!fp1)
    error (EXIT_FAILURE, errno, "%s", names[0]);
  fp2 = strcmp (names[1], "-") ? fopen (names[1], "r") : stdin;
  if (!fp2)
    error (EXIT_FAILURE, errno, "%s", names[1]);
  if (fp1 == fp2)
    error (EXIT_FAILURE, errno, _("both files cannot be standard input"));
  join (fp1, fp2);

  if (fp1 != stdin && fclose (fp1) == EOF)
    error (EXIT_FAILURE, errno, "%s", names[0]);
  if (fp2 != stdin && fclose (fp2) == EOF)
    error (EXIT_FAILURE, errno, "%s", names[1]);
  if ((fp1 == stdin || fp2 == stdin) && fclose (stdin) == EOF)
    error (EXIT_FAILURE, errno, "-");
  if (ferror (stdout) || fclose (stdout) == EOF)
    error (EXIT_FAILURE, errno, _("write error"));

  exit (EXIT_SUCCESS);
}
