/* cut - remove parts of lines of files
   Copyright (C) 1984, 1997-2003 by David M. Ihnat

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

/* Written by David Ihnat.  */

/* POSIX changes, bug fixes, long-named options, and cleanup
   by David MacKenzie <djm@gnu.ai.mit.edu>.

   Rewrite cut_fields and cut_bytes -- Jim Meyering.  */

#include <config.h>

#include <stdio.h>
#include <assert.h>
#include <getopt.h>
#include <sys/types.h>
#include "system.h"

#include "closeout.h"
#include "error.h"
#include "getdelim2.h"
#include "hash.h"
#include "quote.h"
#include "stdbool.h"
#include "xstrndup.h"

/* The official name of this program (e.g., no `g' prefix).  */
#define PROGRAM_NAME "cut"

#define AUTHORS N_ ("David Ihnat, David MacKenzie, and Jim Meyering")

#define FATAL_ERROR(Message)						\
  do									\
    {									\
      error (0, 0, (Message));						\
      usage (2);							\
    }									\
  while (0)

/* Append LOW, HIGH to the list RP of range pairs, allocating additional
   space if necessary.  Update local variable N_RP.  When allocating,
   update global variable N_RP_ALLOCATED.  */

#define ADD_RANGE_PAIR(rp, low, high)					\
  do									\
    {									\
      if (n_rp >= n_rp_allocated)					\
	{								\
	  n_rp_allocated *= 2;						\
	  (rp) = xrealloc (rp, n_rp_allocated * sizeof (*(rp)));	\
	}								\
      rp[n_rp].lo = (low);						\
      rp[n_rp].hi = (high);						\
      ++n_rp;								\
    }									\
  while (0)

struct range_pair
  {
    unsigned int lo;
    unsigned int hi;
  };

/* This buffer is used to support the semantics of the -s option
   (or lack of same) when the specified field list includes (does
   not include) the first field.  In both of those cases, the entire
   first field must be read into this buffer to determine whether it
   is followed by a delimiter or a newline before any of it may be
   output.  Otherwise, cut_fields can do the job without using this
   buffer.  */
static char *field_1_buffer;

/* The number of bytes allocated for FIELD_1_BUFFER.  */
static size_t field_1_bufsize;

/* The largest field or byte index used as an endpoint of a closed
   or degenerate range specification;  this doesn't include the starting
   index of right-open-ended ranges.  For example, with either range spec
   `2-5,9-', `2-3,5,9-' this variable would be set to 5.  */
static unsigned int max_range_endpoint;

/* If nonzero, this is the index of the first field in a range that goes
   to end of line. */
static unsigned int eol_range_start;

/* In byte mode, which bytes to output.
   In field mode, which DELIM-separated fields to output.
   Both bytes and fields are numbered starting with 1,
   so the zeroth element of this array is unused.
   A field or byte K has been selected if
   (K <= MAX_RANGE_ENDPOINT and PRINTABLE_FIELD[K])
    || (EOL_RANGE_START > 0 && K >= EOL_RANGE_START).  */
static int *printable_field;

enum operating_mode
  {
    undefined_mode,

    /* Output characters that are in the given bytes. */
    byte_mode,

    /* Output the given delimeter-separated fields. */
    field_mode
  };

/* The name this program was run with. */
char *program_name;

static enum operating_mode operating_mode;

/* If nonzero do not output lines containing no delimeter characters.
   Otherwise, all such lines are printed.  This option is valid only
   with field mode.  */
static int suppress_non_delimited;

/* The delimeter character for field mode. */
static int delim;

/* Nonzero if the --output-delimiter=STRING option was specified.  */
static int output_delimiter_specified;

/* The length of output_delimiter_string.  */
static size_t output_delimiter_length;

/* The output field separator string.  Defaults to the 1-character
   string consisting of the input delimiter.  */
static char *output_delimiter_string;

/* Nonzero if we have ever read standard input. */
static int have_read_stdin;

#define HT_RANGE_START_INDEX_INITIAL_CAPACITY 31

/* The set of range-start indices.  For example, given a range-spec list like
   `-b1,3-5,4-9,15-', the following indices will be recorded here: 1, 3, 15.
   Note that although `4' looks like a range-start index, it is in the middle
   of the `3-5' range, so it doesn't count.
   This table is created/used IFF output_delimiter_specified is set.  */
static Hash_table *range_start_ht;

/* For long options that have no equivalent short option, use a
   non-character as a pseudo short option, starting with CHAR_MAX + 1.  */
enum
{
  OUTPUT_DELIMITER_OPTION = CHAR_MAX + 1
};

static struct option const longopts[] =
{
  {"bytes", required_argument, 0, 'b'},
  {"characters", required_argument, 0, 'c'},
  {"fields", required_argument, 0, 'f'},
  {"delimiter", required_argument, 0, 'd'},
  {"only-delimited", no_argument, 0, 's'},
  {"output-delimiter", required_argument, 0, OUTPUT_DELIMITER_OPTION},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {0, 0, 0, 0}
};

void
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
      fputs (_("\
Print selected parts of lines from each FILE to standard output.\n\
\n\
"), stdout);
      fputs (_("\
Mandatory arguments to long options are mandatory for short options too.\n\
"), stdout);
      fputs (_("\
  -b, --bytes=LIST        output only these bytes\n\
  -c, --characters=LIST   output only these characters\n\
  -d, --delimiter=DELIM   use DELIM instead of TAB for field delimiter\n\
"), stdout);
      fputs (_("\
  -f, --fields=LIST       output only these fields;  also print any line\n\
                            that contains no delimiter character, unless\n\
                            the -s option is specified\n\
  -n                      (ignored)\n\
"), stdout);
      fputs (_("\
  -s, --only-delimited    do not print lines not containing delimiters\n\
      --output-delimiter=STRING  use STRING as the output delimiter\n\
                            the default is to use the input delimiter\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      fputs (_("\
\n\
Use one, and only one of -b, -c or -f.  Each LIST is made up of one\n\
range, or many ranges separated by commas.  Each range is one of:\n\
\n\
  N     N'th byte, character or field, counted from 1\n\
  N-    from N'th byte, character or field, to end of line\n\
  N-M   from N'th to M'th (included) byte, character or field\n\
  -M    from first to M'th (included) byte, character or field\n\
\n\
With no FILE, or when FILE is -, read standard input.\n\
"), stdout);
      printf (_("\nReport bugs to <%s>.\n"), PACKAGE_BUGREPORT);
    }
  exit (status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

unsigned int
hash_int (const void *x, unsigned int tablesize)
{
  unsigned int y = (unsigned int) x;
  return (y % tablesize);
}

static bool
hash_compare_ints (void const *x, void const *y)
{
  return (x == y) ? true : false;
}

static bool
is_range_start_index (int i)
{
  return hash_lookup (range_start_ht, (void *) i) ? true : false;
}

/* Return nonzero if the K'th field or byte is printable.
   When returning nonzero, if RANGE_START is non-NULL,
   set *RANGE_START to nonzero if K is the beginning of a range, and
   set *RANGE_START to zero if K is not the beginning of a range.  */

static int
print_kth (unsigned int k, int *range_start)
{
  if (0 < eol_range_start && eol_range_start <= k)
    {
      if (range_start)
	*range_start = (k == eol_range_start);
      return 1;
    }

  if (k <= max_range_endpoint && printable_field[k])
    {
      if (range_start)
	*range_start = is_range_start_index (k);
      return 1;
    }

  return 0;
}

/* Given the list of field or byte range specifications FIELDSTR, set
   MAX_RANGE_ENDPOINT and allocate and initialize the PRINTABLE_FIELD
   array.  If there is a right-open-ended range, set EOL_RANGE_START
   to its starting index.  FIELDSTR should be composed of one or more
   numbers or ranges of numbers, separated by blanks or commas.
   Incomplete ranges may be given: `-m' means `1-m'; `n-' means `n'
   through end of line.  Return nonzero if FIELDSTR contains at least
   one field specification, zero otherwise.  */

/* FIXME-someday:  What if the user wants to cut out the 1,000,000-th field
   of some huge input file?  This function shouldn't have to allocate a table
   of a million ints just so we can test every field < 10^6 with an array
   dereference.  Instead, consider using a dynamic hash table.  It would be
   simpler and nearly as good a solution to use a 32K x 4-byte table with
   one bit per field index instead of a whole `int' per index.  */

static int
set_fields (const char *fieldstr)
{
  unsigned int initial = 1;	/* Value of first number in a range.  */
  unsigned int value = 0;	/* If nonzero, a number being accumulated.  */
  int dash_found = 0;		/* Nonzero if a '-' is found in this field.  */
  int field_found = 0;		/* Non-zero if at least one field spec
				   has been processed.  */

  struct range_pair *rp;
  unsigned int n_rp;
  unsigned int n_rp_allocated;
  unsigned int i;
  bool in_digits = false;

  n_rp = 0;
  n_rp_allocated = 16;
  rp = xmalloc (n_rp_allocated * sizeof (*rp));

  /* Collect and store in RP the range end points.
     It also sets EOL_RANGE_START if appropriate.  */

  for (;;)
    {
      if (*fieldstr == '-')
	{
	  in_digits = false;
	  /* Starting a range. */
	  if (dash_found)
	    FATAL_ERROR (_("invalid byte or field list"));
	  dash_found++;
	  fieldstr++;

	  if (value)
	    {
	      initial = value;
	      value = 0;
	    }
	  else
	    initial = 1;
	}
      else if (*fieldstr == ',' || ISBLANK (*fieldstr) || *fieldstr == '\0')
	{
	  in_digits = false;
	  /* Ending the string, or this field/byte sublist. */
	  if (dash_found)
	    {
	      dash_found = 0;

	      /* A range.  Possibilites: -n, m-n, n-.
		 In any case, `initial' contains the start of the range. */
	      if (value == 0)
		{
		  /* `n-'.  From `initial' to end of line. */
		  eol_range_start = initial;
		  field_found = 1;
		}
	      else
		{
		  /* `m-n' or `-n' (1-n). */
		  if (value < initial)
		    FATAL_ERROR (_("invalid byte or field list"));

		  /* Is there already a range going to end of line? */
		  if (eol_range_start != 0)
		    {
		      /* Yes.  Is the new sequence already contained
			 in the old one?  If so, no processing is
			 necessary. */
		      if (initial < eol_range_start)
			{
			  /* No, the new sequence starts before the
			     old.  Does the old range going to end of line
			     extend into the new range?  */
			  if (eol_range_start <= value)
			    {
			      /* Yes.  Simply move the end of line marker. */
			      eol_range_start = initial;
			    }
			  else
			    {
			      /* No.  A simple range, before and disjoint from
				 the range going to end of line.  Fill it. */
			      ADD_RANGE_PAIR (rp, initial, value);
			    }

			  /* In any case, some fields were selected. */
			  field_found = 1;
			}
		    }
		  else
		    {
		      /* There is no range going to end of line. */
		      ADD_RANGE_PAIR (rp, initial, value);
		      field_found = 1;
		    }
		  value = 0;
		}
	    }
	  else if (value != 0)
	    {
	      /* A simple field number, not a range. */
	      ADD_RANGE_PAIR (rp, value, value);
	      value = 0;
	      field_found = 1;
	    }

	  if (*fieldstr == '\0')
	    {
	      break;
	    }

	  fieldstr++;
	}
      else if (ISDIGIT (*fieldstr))
	{
	  unsigned int prev;
	  /* Record beginning of digit string, in case we have to
	     complain about it.  */
	  static char const *num_start;
	  if (!in_digits || !num_start)
	    num_start = fieldstr;
	  in_digits = true;

	  /* Detect overflow.  */
	  prev = value;
	  value = 10 * value + *fieldstr - '0';
	  if (value < prev)
	    {
	      /* In case the user specified -c4294967296-22,
		 complain only about the first number.  */
	      /* Determine the length of the offending number.  */
	      size_t len = strspn (num_start, "0123456789");
	      char *bad_num = xstrndup (num_start, len);
	      if (operating_mode == byte_mode)
		error (0, 0,
		       _("byte offset %s is too large"), quote (bad_num));
	      else
		error (0, 0,
		       _("field number %s is too large"), quote (bad_num));
	      free (bad_num);
	      exit (EXIT_FAILURE);
	    }
	  fieldstr++;
	}
      else
	FATAL_ERROR (_("invalid byte or field list"));
    }

  max_range_endpoint = 0;
  for (i = 0; i < n_rp; i++)
    {
      if (rp[i].hi > max_range_endpoint)
	max_range_endpoint = rp[i].hi;
    }

  /* Allocate an array large enough so that it may be indexed by
     the field numbers corresponding to all finite ranges
     (i.e. `2-6' or `-4', but not `5-') in FIELDSTR.  */

  printable_field = xmalloc ((max_range_endpoint + 1) * sizeof (int));
  memset (printable_field, 0, (max_range_endpoint + 1) * sizeof (int));

  /* Set the array entries corresponding to integers in the ranges of RP.  */
  for (i = 0; i < n_rp; i++)
    {
      unsigned int j;
      for (j = rp[i].lo; j <= rp[i].hi; j++)
	{
	  printable_field[j] = 1;
	}
    }

  if (output_delimiter_specified)
    {
      /* Record the range-start indices.  */
      for (i = 0; i < n_rp; i++)
	{
	  unsigned int j = rp[i].lo;
	  for (j = rp[i].lo; j <= rp[i].hi; j++)
	    {
	      if (0 < j && printable_field[j] && !printable_field[j - 1])
		{
		  /* Remember that `j' is a range-start index.  */
		  void *ent_from_table = hash_insert (range_start_ht,
						      (void*) j);
		  if (ent_from_table == NULL)
		    {
		      /* Insertion failed due to lack of memory.  */
		      xalloc_die ();
		    }
		  assert ((unsigned int) ent_from_table == j);
		}
	    }
	}
    }

  free (rp);

  return field_found;
}

/* Read from stream STREAM, printing to standard output any selected bytes.  */

static void
cut_bytes (FILE *stream)
{
  unsigned int byte_idx;	/* Number of bytes in the line so far. */
  /* Whether to begin printing delimiters between ranges for the current line.
     Set after we've begun printing data corresponding to the first range.  */
  int print_delimiter;

  byte_idx = 0;
  print_delimiter = 0;
  while (1)
    {
      register int c;		/* Each character from the file. */

      c = getc (stream);

      if (c == '\n')
	{
	  putchar ('\n');
	  byte_idx = 0;
	  print_delimiter = 0;
	}
      else if (c == EOF)
	{
	  if (byte_idx > 0)
	    putchar ('\n');
	  break;
	}
      else
	{
	  int range_start;
	  int *rs = output_delimiter_specified ? &range_start : NULL;
	  if (print_kth (++byte_idx, rs))
	    {
	      if (rs && *rs && print_delimiter)
		{
		  fwrite (output_delimiter_string, sizeof (char),
			  output_delimiter_length, stdout);
		}
	      print_delimiter = 1;
	      putchar (c);
	    }
	}
    }
}

/* Read from stream STREAM, printing to standard output any selected fields.  */

static void
cut_fields (FILE *stream)
{
  int c;
  unsigned int field_idx;
  int found_any_selected_field;
  int buffer_first_field;
  int empty_input;

  found_any_selected_field = 0;
  field_idx = 1;

  c = getc (stream);
  empty_input = (c == EOF);
  if (c != EOF)
    ungetc (c, stream);

  /* To support the semantics of the -s flag, we may have to buffer
     all of the first field to determine whether it is `delimited.'
     But that is unnecessary if all non-delimited lines must be printed
     and the first field has been selected, or if non-delimited lines
     must be suppressed and the first field has *not* been selected.
     That is because a non-delimited line has exactly one field.  */
  buffer_first_field = (suppress_non_delimited ^ !print_kth (1, NULL));

  while (1)
    {
      if (field_idx == 1 && buffer_first_field)
	{
	  int len;
	  size_t n_bytes;

	  len = getdelim2 (&field_1_buffer, &field_1_bufsize, stream,
			   delim, '\n', 0);
	  if (len < 0)
	    {
	      if (ferror (stream) || feof (stream))
		break;
	      xalloc_die ();
	    }

	  n_bytes = len;
	  assert (n_bytes != 0);

	  /* If the first field extends to the end of line (it is not
	     delimited) and we are printing all non-delimited lines,
	     print this one.  */
	  if ((unsigned char) field_1_buffer[n_bytes - 1] != delim)
	    {
	      if (suppress_non_delimited)
		{
		  /* Empty.  */
		}
	      else
		{
		  fwrite (field_1_buffer, sizeof (char), n_bytes, stdout);
		  /* Make sure the output line is newline terminated.  */
		  if (field_1_buffer[n_bytes - 1] != '\n')
		    putchar ('\n');
		}
	      continue;
	    }
	  if (print_kth (1, NULL))
	    {
	      /* Print the field, but not the trailing delimiter.  */
	      fwrite (field_1_buffer, sizeof (char), n_bytes - 1, stdout);
	      found_any_selected_field = 1;
	    }
	  ++field_idx;
	}

      if (c != EOF)
	{
	  if (print_kth (field_idx, NULL))
	    {
	      if (found_any_selected_field)
		{
		  fwrite (output_delimiter_string, sizeof (char),
			  output_delimiter_length, stdout);
		}
	      found_any_selected_field = 1;

	      while ((c = getc (stream)) != delim && c != '\n' && c != EOF)
		{
		  putchar (c);
		}
	    }
	  else
	    {
	      while ((c = getc (stream)) != delim && c != '\n' && c != EOF)
		{
		  /* Empty.  */
		}
	    }
	}

      if (c == '\n')
	{
	  c = getc (stream);
	  if (c != EOF)
	    {
	      ungetc (c, stream);
	      c = '\n';
	    }
	}

      if (c == delim)
	++field_idx;
      else if (c == '\n' || c == EOF)
	{
	  if (found_any_selected_field
	      || (!empty_input && !(suppress_non_delimited && field_idx == 1)))
	    putchar ('\n');
	  if (c == EOF)
	    break;
	  field_idx = 1;
	  found_any_selected_field = 0;
	}
    }
}

static void
cut_stream (FILE *stream)
{
  if (operating_mode == byte_mode)
    cut_bytes (stream);
  else
    cut_fields (stream);
}

/* Process file FILE to standard output.
   Return 0 if successful, 1 if not. */

static int
cut_file (char *file)
{
  FILE *stream;

  if (STREQ (file, "-"))
    {
      have_read_stdin = 1;
      stream = stdin;
    }
  else
    {
      stream = fopen (file, "r");
      if (stream == NULL)
	{
	  error (0, errno, "%s", file);
	  return 1;
	}
    }

  cut_stream (stream);

  if (ferror (stream))
    {
      error (0, errno, "%s", file);
      return 1;
    }
  if (STREQ (file, "-"))
    clearerr (stream);		/* Also clear EOF. */
  else if (fclose (stream) == EOF)
    {
      error (0, errno, "%s", file);
      return 1;
    }
  return 0;
}

int
main (int argc, char **argv)
{
  int optc, exit_status = 0;
  int delim_specified = 0;
  char *spec_list_string IF_LINT(= NULL);

  initialize_main (&argc, &argv);
  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdout);

  operating_mode = undefined_mode;

  /* By default, all non-delimited lines are printed.  */
  suppress_non_delimited = 0;

  delim = '\0';
  have_read_stdin = 0;

  while ((optc = getopt_long (argc, argv, "b:c:d:f:ns", longopts, NULL)) != -1)
    {
      switch (optc)
	{
	case 0:
	  break;

	case 'b':
	case 'c':
	  /* Build the byte list. */
	  if (operating_mode != undefined_mode)
	    FATAL_ERROR (_("only one type of list may be specified"));
	  operating_mode = byte_mode;
	  spec_list_string = optarg;
	  break;

	case 'f':
	  /* Build the field list. */
	  if (operating_mode != undefined_mode)
	    FATAL_ERROR (_("only one type of list may be specified"));
	  operating_mode = field_mode;
	  spec_list_string = optarg;
	  break;

	case 'd':
	  /* New delimiter. */
	  /* Interpret -d '' to mean `use the NUL byte as the delimiter.'  */
	  if (optarg[0] != '\0' && optarg[1] != '\0')
	    FATAL_ERROR (_("the delimiter must be a single character"));
	  delim = (unsigned char) optarg[0];
	  delim_specified = 1;
	  break;

	case OUTPUT_DELIMITER_OPTION:
	  output_delimiter_specified = 1;
	  /* Interpret --output-delimiter='' to mean
	     `use the NUL byte as the delimiter.'  */
	  output_delimiter_length = (optarg[0] == '\0'
				     ? 1 : strlen (optarg));
	  output_delimiter_string = xstrdup (optarg);
	  break;

	case 'n':
	  break;

	case 's':
	  suppress_non_delimited = 1;
	  break;

	case_GETOPT_HELP_CHAR;

	case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

	default:
	  usage (2);
	}
    }

  if (operating_mode == undefined_mode)
    FATAL_ERROR (_("you must specify a list of bytes, characters, or fields"));

  if (delim != '\0' && operating_mode != field_mode)
    FATAL_ERROR (_("an input delimiter may be specified only\
 when operating on fields"));

  if (suppress_non_delimited && operating_mode != field_mode)
    FATAL_ERROR (_("suppressing non-delimited lines makes sense\n\
\tonly when operating on fields"));

  if (output_delimiter_specified)
    {
      range_start_ht = hash_initialize (HT_RANGE_START_INDEX_INITIAL_CAPACITY,
					NULL, hash_int,
					hash_compare_ints, NULL);
      if (range_start_ht == NULL)
	xalloc_die ();

    }

  if (set_fields (spec_list_string) == 0)
    {
      if (operating_mode == field_mode)
	FATAL_ERROR (_("missing list of fields"));
      else
	FATAL_ERROR (_("missing list of positions"));
    }

  if (!delim_specified)
    delim = '\t';

  if (output_delimiter_string == NULL)
    {
      static char dummy[2];
      dummy[0] = delim;
      dummy[1] = '\0';
      output_delimiter_string = dummy;
      output_delimiter_length = 1;
    }

  if (optind == argc)
    exit_status |= cut_file ("-");
  else
    for (; optind < argc; optind++)
      exit_status |= cut_file (argv[optind]);

  if (range_start_ht)
    hash_free (range_start_ht);

  if (have_read_stdin && fclose (stdin) == EOF)
    {
      error (0, errno, "-");
      exit_status = 1;
    }

  exit (exit_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
