/* pr -- convert text files for printing.
   Copyright (C) 1988, 1991, 1995, 1996 Free Software Foundation, Inc.

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

/*  Author: Pete TerMaat.  */

/* Things to watch: Sys V screws up on ...
   pr -n -3 -s: /usr/dict/words
   pr -m -o10 -n /usr/dict/words{,,,}
   pr -6 -a -n -o5 /usr/dict/words

   Ideas:

   Keep a things_to_do list of functions to call when we know we have
   something to print.  Cleaner than current series of checks.

   Improve the printing of control prefixes.


   Options:

   +FIRST_PAGE[:LAST_PAGE]
		begin [stop] printing with page FIRST_[LAST_]PAGE

   -COLUMN	Produce output that is COLUMN columns wide and print
		columns down.
                Balance columns on the last page is automatically set.

   -a		Print columns across rather than down.  The input
		one
		two
		three
		four
		will be printed as
		one	two	three
		four

   -b		Balance columns on the last page.
                -b is no longer an independent option. It's always used
		together with -COLUMN (unless -a is used) to get a
		consistent formulation with "FF set by hand" in input
		files. Each formfeed found terminates the number of lines
		to be read with the actual page. The situation for
		printing columns down is equivalent to that on the last
		page. So we need a balancing.

		We do not yet eliminate source text dealing with -COLUMN
		only. Tune this if it proved to be a satisfactory
		procedure.

		Keeping -b as an underground option guarantees some
		downward compatibility. Utilities using pr with -b
		(a most frequently used form) still work as usual.

   -c		Print unprintable characters as control prefixes.
		Control-g is printed as ^G.

   -d		Double space the output.

   -e[c[k]]	Expand tabs to spaces on input.  Optional argument C
		is the input tab character. (Default is `\t'.)  Optional
		argument K is the input tab character's width.  (Default is 8.)

   -F
   -f		Use formfeeds instead of newlines to separate pages.
		A three line HEADER is used, no TRAILER (without -f
		both HEADER and TRAILER are made of five lines).

   -h HEADER	Replace the filename in the header with the string HEADER.
                Checking and left-hand-side truncation of the length of the
                standard and custom header string. A centered header is used.
                The format of date and time has been shortened
		to  yy-mm-dd HH:MM  to give place to a maximal filename
		information.
                -h ""  now prints a blank line header. -h"" shows an error.

   -i[c[k]]	Replace spaces with tabs on output.  Optional argument
		C is the output tab character.  (Default is `\t'.)  Optional
		argument K is the output tab character's width.  (Default
		is 8.)

  -j            Merge full lines, turns off -w line truncation, no column
                alignment, -s[STRING] sets separators, works with all
		column options (-COLUMN | -a -COLUMN | -m).

   -l lines	Set the page length to LINES.  Default is 66.

   -m		Print files in parallel; pad_across_to align columns;
		truncate lines and print separator strings;
		Do it also with empty columns to get a continuous line
		numbering and column marking by separators throughout
		the whole merged file.

		Empty pages in some input files produce empty columns
		[marked by separators] in the common pages. Completely
		empty common pages show no column separators at all.

		The layout of a merged page is ruled by the largest form
		feed distance of the single pages at that page. Shorter
		columns will be filled up with empty lines.

		Together with -j option join lines of full length and
		in addition set separators when -s option is used.

   -n[c[k]]	Precede each column with a line number.
		(With parallel files, precede each line with a line
		number.)  Optional argument C is the character to print
		after each number.  (Default `\t'.)  Optional argument
		k is the number of digits per line number.  (Default 5.)
		Default counting starts with 1st line of input file.

  -N number	Start counting with  number  at 1st line of first page
		printed.

   -o offset	Offset each line with a margin OFFSET spaces wide.
		Total page width is the size of this offset plus the
		width set with `-w'.

   -r		Ignore files that can't be opened.

  -s[STRING]	Separate columns by any string STRING.
                Don't use -s "STRING".
		without -s: default separator 'space' s used,
		same as -s" ",
		with -s only: no separator is used, same as -s"".
	        Quotes should be used with blanks and some shell active
		characters.

   -t		Do not print headers or footers but retain form feeds
		set in input files (some page layout is not changed).

   -T		Do not print headers or footers, eliminate form feeds
		in input files.

   -v		Print unprintable characters as escape sequences.
		Control-G becomes \007.

   -w width	Set the page width to WIDTH characters.
		(In pr versions newer than 1.14 -s option does no longer
		affect -w option.)

	        With/without  -w width  the header line is truncated.
	        Default is 72 characters.
	        With  -w width  text lines will be truncated, unless -j is
		used. Together with one of the column options
		(-COLUMN| -a -COLUMN| -m) column alignment is used.
	        Without  -w PAGE_WIDTH
	        - but with one of the column options default truncation of
		  72 characters is used (to keep downward compatibility
		  and to simplify most frequently met column tasks).
		  Column alignment and column separators are used.
	        - and without any of the column options no line truncation
	 	  is used (to keep downward compatibility and to meet most
		  frequent tasks). That's equivalent to  -w 72 -j .
*/


#include <config.h>

#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <time.h>
#include "system.h"
#include "error.h"
#include "xstrtol.h"

#if HAVE_LIMITS_H
#include <limits.h>
#endif

char *xmalloc ();
char *xrealloc ();

#ifndef UINT_MAX
#define UINT_MAX ((unsigned int) ~(unsigned int) 0)
#endif

#ifndef INT_MAX
#define INT_MAX ((int) (UINT_MAX >> 1))
#endif

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

/* Used with start_position in the struct COLUMN described below.
   If start_position == ANYWHERE, we aren't truncating columns and
   can begin printing a column anywhere.  Otherwise we must pad to
   the horizontal position start_position. */
#define ANYWHERE	0

/* Each column has one of these structures allocated for it.
   If we're only dealing with one file, fp is the same for all
   columns.

   The general strategy is to spend time setting up these column
   structures (storing columns if necessary), after which printing
   is a matter of flitting from column to column and calling
   print_func.

   Parallel files, single files printing across in multiple
   columns, and single files printing down in multiple columns all
   fit the same printing loop.

   print_func		Function used to print lines in this column.
			If we're storing this column it will be
			print_stored(), Otherwise it will be read_line().

   char_func		Function used to process characters in this column.
			If we're storing this column it will be store_char(),
			otherwise it will be print_char().

   current_line		Index of the current entry in line_vector, which
			contains the index of the first character of the
			current line in buff[].

   lines_stored		Number of lines in this column which are stored in
			buff.

   lines_to_print	If we're storing this column, lines_to_print is
			the number of stored_lines which remain to be
			printed.  Otherwise it is the number of lines
			we can print without exceeding lines_per_body.

   start_position	The horizontal position we want to be in before we
			print the first character in this column.

   numbered		True means precede this column with a line number. */

struct COLUMN
  {
    FILE *fp;			/* Input stream for this column. */
    char *name;			/* File name. */
    enum
      {
	OPEN,
	FF_FOUND,		/* used with -b option, set with \f, changed
				   to ON_HOLD after print_header */
	ON_HOLD,		/* Hit a form feed. */
	CLOSED
      }
    status;			/* Status of the file pointer. */
    int (*print_func) ();	/* Func to print lines in this col. */
    void (*char_func) ();	/* Func to print/store chars in this col. */
    int current_line;		/* Index of current place in line_vector. */
    int lines_stored;		/* Number of lines stored in buff. */
    int lines_to_print;		/* No. lines stored or space left on page. */
    int start_position;		/* Horizontal position of first char. */
    int numbered;
    int full_page_printed;	/* True means printed without a FF found. */

    /* p->full_page_printed  controls a special case of "FF set by hand":
       True means a full page has been printed without FF found. To avoid an
       additional empty page we have to ignore a FF immediately following in
       the next line. */
  };

typedef struct COLUMN COLUMN;

#define NULLCOL (COLUMN *)0

static int char_to_clump __P ((int c));
static int read_line __P ((COLUMN *p));
static int print_page __P ((void));
static int print_stored __P ((COLUMN *p));
static int open_file __P ((char *name, COLUMN *p));
static int skip_to_page __P ((int page));
static void print_header __P ((void));
static void pad_across_to __P ((int position));
static void number __P ((COLUMN *p));
static void getoptarg __P ((char *arg, char switch_char, char *character, int *number));
static void usage __P ((int status));
static void print_files __P ((int number_of_files, char **av));
static void init_parameters __P ((int number_of_files));
static void init_header __P ((char *filename, int desc));
static void init_store_cols __P ((void));
static void store_columns __P ((void));
static void balance __P ((int total_stored));
static void store_char __P ((int c));
static void pad_down __P ((int lines));
static void read_rest_of_line __P ((COLUMN *p));
static void skip_read __P ((COLUMN *p, int column_number));
static void print_char __P ((int c));
static void cleanup __P ((void));
static void first_last_page __P ((char *pages));
static void print_sep_string __P ((void));
static void separator_string __P ((const char *optarg_S));

/* The name under which this program was invoked. */
char *program_name;

/* All of the columns to print.  */
static COLUMN *column_vector;

/* When printing a single file in multiple downward columns,
   we store the leftmost columns contiguously in buff.
   To print a line from buff, get the index of the first character
   from line_vector[i], and print up to line_vector[i + 1]. */
static char *buff;

/* Index of the position in buff where the next character
   will be stored. */
static int buff_current;

/* The number of characters in buff.
   Used for allocation of buff and to detect overflow of buff. */
static int buff_allocated;

/* Array of indices into buff.
   Each entry is an index of the first character of a line.
   This is used when storing lines to facilitate shuffling when
   we do column balancing on the last page. */
static int *line_vector;

/* Array of horizonal positions.
   For each line in line_vector, end_vector[line] is the horizontal
   position we are in after printing that line.  We keep track of this
   so that we know how much we need to pad to prepare for the next
   column. */
static int *end_vector;

/* (-m) True means we're printing multiple files in parallel. */
static int parallel_files = FALSE;

/* (-m) True means a line starts with some empty columns (some files
   already CLOSED or ON_HOLD) which we have to align. */
static int align_empty_cols;

/* (-m) True means we have not yet found any printable column in a line.
   align_empty_cols = TRUE  has to be maintained. */
static int empty_line;

/* (-m) False means printable column output precedes a form feed found.
   Column align is done only once. No additional action with that form
   feed.
   True means we found only a form feed in a column. Maybe we have to do
   some column align with that form feed. */
static int FF_only;

/* (-[0-9]+) True means we're given an option explicitly specifying
   number of columns.  Used to detect when this option is used with -m. */
static int explicit_columns = FALSE;

/* (-t|-T) False means we aren't printing headers and footers. */
static int extremities = TRUE;

/* (-t) True means we retain all FF set by hand in input files.
   False is set with -T option. */
static int keep_FF = FALSE;
static int print_a_FF = FALSE;

/* True means we need to print a header as soon as we know we've got input
   to print after it. */
static int print_a_header;

/* (-h) True means we're using the standard header rather than a
   customized one specified by the -h flag. */
static int standard_header = TRUE;

/* (-f) True means use formfeeds instead of newlines to separate pages. */
static int use_form_feed = FALSE;

/* True means we have read the standard input. */
static int have_read_stdin = FALSE;

/* True means the -a flag has been given. */
static int print_across_flag = FALSE;

/* True means we're printing one file in multiple (>1) downward columns. */
static int storing_columns = TRUE;

/* (-b) True means balance columns on the last page as Sys V does. */
/* That's no longer an independent option. With storing_columns = TRUE
   balance_columns = TRUE is used too (s. function init_parameters).
   We get a consistent formulation with "FF set by hand" in input files. */
static int balance_columns = FALSE;

/* (-l) Number of lines on a page, including header and footer lines. */
static int lines_per_page = 66;

/* Number of lines in the header and footer can be reset to 0 using
   the -t flag. */
static int lines_per_header = 5;
static int lines_per_body;
static int lines_per_footer = 5;

/* (-w) Width in characters of the page.  Does not include the width of
   the margin. */
static int chars_per_line = 72;

/* (-w) True means we truncate lines longer than chars_per_column. */
static int truncate_lines = FALSE;

/* (-j) True means we join lines without any line truncation. -j
   dominates -w option. */
static int join_lines = FALSE;

/* Number of characters in a column.  Based on col_sep_length and
   page width. */
static int chars_per_column;

/* (-e) True means convert tabs to spaces on input. */
static int untabify_input = FALSE;

/* (-e) The input tab character. */
static char input_tab_char = '\t';

/* (-e) Tabstops are at chars_per_tab, 2*chars_per_tab, 3*chars_per_tab, ...
   where the leftmost column is 1. */
static int chars_per_input_tab = 8;

/* (-i) True means convert spaces to tabs on output. */
static int tabify_output = FALSE;

/* (-i) The output tab character. */
static char output_tab_char = '\t';

/* (-i) The width of the output tab. */
static int chars_per_output_tab = 8;

/* Keeps track of pending white space.  When we hit a nonspace
   character after some whitespace, we print whitespace, tabbing
   if necessary to get to output_position + spaces_not_printed. */
static int spaces_not_printed;

/* (-o) Number of spaces in the left margin (tabs used when possible). */
static int chars_per_margin = 0;

/* Position where the next character will fall.
   Leftmost position is 0 + chars_per_margin.
   Rightmost position is chars_per_margin + chars_per_line - 1.
   This is important for converting spaces to tabs on output. */
static int output_position;

/* Horizontal position relative to the current file.
   (output_position depends on where we are on the page;
   input_position depends on where we are in the file.)
   Important for converting tabs to spaces on input. */
static int input_position;

/* Count number of failed opens so we can exit with nonzero
   status if there were any.  */
static int failed_opens = 0;

/* The horizontal position we'll be at after printing a tab character
   of width c_ from the position h_. */
#define pos_after_tab(c_, h_) h_ - h_ % c_ + c_

/* The number of spaces taken up if we print a tab character with width
   c_ from position h_. */
#define tab_width(c_, h_) - h_ % c_ + c_

/* (-NNN) Number of columns of text to print. */
static int columns = 1;

/* (+NNN:MMM) Page numbers on which to begin and stop printing. */
static int first_page_number = 1;
static int last_page_number = 0;

/* Number of files open (not closed, not on hold). */
static int files_ready_to_read = 0;

/* Current page number.  Displayed in header. */
static int page_number;

/* Current line number.  Displayed when -n flag is specified.

   When printing files in parallel (-m flag), line numbering is as follows:
   1    foo     goo     moo
   2    hoo     too     zoo

   When printing files across (-a flag), ...
   1    foo     2       moo     3       goo
   4    hoo     3       too     6       zoo

   Otherwise, line numbering is as follows:
   1    foo     3       goo     5       too
   2    moo     4       hoo     6       zoo */
static int line_number;

/* (-n) True means lines should be preceded by numbers. */
static int numbered_lines = FALSE;

/* (-n) Character which follows each line number. */
static char number_separator = '\t';

/* (-n) line counting starts with 1st line of input file (not with 1st
   line of 1st page printed). */
static int line_count = 1;

/* (-n) True means counting of skipped lines starts with 1st line of
   input file. False means -N option is used in addition, counting of
   skipped lines not required. */
static int skip_count = TRUE;

/* (-N) Counting starts with start_line_number = NUMBER at 1st line of
   first page printed, usually not 1st page of input file. */
static int start_line_num = 1;

/* (-n) Width in characters of a line number. */
static int chars_per_number = 5;

/* Used when widening the first column to accommodate numbers -- only
   needed when printing files in parallel.  Includes width of both the
   number and the number_separator. */
static int number_width;

/* Buffer sprintf uses to format a line number. */
static char *number_buff;

/* (-v) True means unprintable characters are printed as escape sequences.
   control-g becomes \007. */
static int use_esc_sequence = FALSE;

/* (-c) True means unprintable characters are printed as control prefixes.
   control-g becomes ^G. */
static int use_cntrl_prefix = FALSE;

/* (-d) True means output is double spaced. */
static int double_space = FALSE;

/* Number of files opened initially in init_files.  Should be 1
   unless we're printing multiple files in parallel. */
static int total_files = 0;

/* (-r) True means don't complain if we can't open a file. */
static int ignore_failed_opens = FALSE;

/* (-s) True means we separate columns with a specified string.
   -s option does not affect line truncation nor column alignment. */
static int use_col_separator = FALSE;

/* String used to separate columns if the -s option has been specified.
   Default value with -s is a space. */
static char *col_sep_string;
static int col_sep_length = 0;
static char *column_separator = " ";

/* Number of separator characters waiting to be printed as soon as we
   know that we have any input remaining to be printed. */
static int separators_not_printed;

/* Position we need to pad to, as soon as we know that we have input
   remaining to be printed. */
static int padding_not_printed;

/* True means we should pad the end of the page.  Remains false until we
   know we have a page to print. */
static int pad_vertically;

/* (-h) String of characters used in place of the filename in the header. */
static char *custom_header;

/* String containing the date, filename or custom header, and "Page ". */
static char *header;

static int *clump_buff;

/* If nonzero, display usage information and exit.  */
static int show_help;

/* If nonzero, print the version on standard output then exit.  */
static int show_version;

/* True means we read the line no. lines_per_body in skip_read
   called by skip_to_page. That variable controls the coincidence of a
   "FF set by hand" and "full_page_printed", see above the definition of
   structure COLUMN. */
static int last_line = FALSE;

/* If nonzero, print a non-variable date and time with the header
   -h HEADER using pr test-suite */
static int test_suite;

static struct option const long_options[] =
{
  {"help", no_argument, &show_help, 1},
  {"version", no_argument, &show_version, 1},
  {"test", no_argument, &test_suite, 1},
  {0, 0, 0, 0}
};

/* Return the number of columns that have either an open file or
   stored lines. */

static int
cols_ready_to_print (void)
{
  COLUMN *q;
  int i;
  int n;

  n = 0;
  for (q = column_vector, i = 0; i < columns; ++q, ++i)
    if (q->status == OPEN ||
	q->status == FF_FOUND ||	/* With -b: To print a header only */
	(storing_columns && q->lines_stored > 0 && q->lines_to_print > 0))
      ++n;
  return n;
}

/* Estimate first_ / last_page_number
   using option +FIRST_PAGE:LAST_PAGE */

static void
first_last_page (char *pages)
{
  char *str1;

  if (*pages == ':')
    {
      error (0, 0, _("invalid range of page numbers: `%s'"), pages);
      usage (2);
    }

  str1 = strchr (pages, ':');
  if (str1 != NULL)
    *str1 = '\0';

  {
    long int tmp_long;
    if (xstrtol (pages, NULL, 10, &tmp_long, NULL) != LONGINT_OK
	|| tmp_long <= 0 || tmp_long > INT_MAX)
      error (EXIT_FAILURE, 0, _("invalid starting page number: `%s'"),
	     pages);
    first_page_number = (int) tmp_long;
  }

  if (str1 == NULL)
    return;

  {
    long int tmp_long;
    if (xstrtol (str1 + 1, NULL, 10, &tmp_long, NULL) != LONGINT_OK
	|| tmp_long <= 0 || tmp_long > INT_MAX)
      error (EXIT_FAILURE, 0, _("invalid ending page number: `%s'"),
	     str1 + 1);
    last_page_number = (int) tmp_long;
  }

  if (first_page_number > last_page_number)
    error (EXIT_FAILURE, 0,
	   _("starting page number is larger than ending page number"));
}

/* Estimate length of col_sep_string with option -s[STRING] */

static void
separator_string (const char *optarg_S)
{
  col_sep_length = (int) strlen (optarg_S);
  col_sep_string = (char *) xmalloc (col_sep_length + 1);
  strcpy (col_sep_string, optarg_S);
}

int
main (int argc, char **argv)
{
  int c;
  int accum = 0;
  int n_files;
  char **file_names;

  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  n_files = 0;
  file_names = (argc > 1
		? (char **) xmalloc ((argc - 1) * sizeof (char *))
		: NULL);

  while (1)
    {
      c = getopt_long (argc, argv,
		       "-0123456789abcde::fFh:i::jl:mn::N:o:rs::tTvw:",
		       long_options, (int *) 0);
      if (c == 1)		/* Non-option argument. */
	{
	  char *s;
	  s = optarg;
	  if (*s == '+')
	    {
	      ++s;
	      first_last_page (s);
	    }
	  else
	    {
	      file_names[n_files++] = optarg;
	    }
	}
      else
	{
	  if (ISDIGIT (c))
	    {
	      accum = accum * 10 + c - '0';
	      continue;
	    }
	  else
	    {
	      if (accum > 0)
		{
		  columns = accum;
		  explicit_columns = TRUE;
		  accum = 0;
		}
	    }
	}

      if (c == 1)
	continue;

      if (c == EOF)
	break;

      switch (c)
	{
	case 0:		/* getopt long option */
	  break;

	case 'a':
	  print_across_flag = TRUE;
	  storing_columns = FALSE;
	  break;
	case 'b':
	  balance_columns = TRUE;
	  break;
	case 'c':
	  use_cntrl_prefix = TRUE;
	  break;
	case 'd':
	  double_space = TRUE;
	  break;
	case 'e':
	  if (optarg)
	    getoptarg (optarg, 'e', &input_tab_char,
		       &chars_per_input_tab);
	  /* Could check tab width > 0. */
	  untabify_input = TRUE;
	  break;
	case 'f':
	case 'F':
	  use_form_feed = TRUE;
	  break;
	case 'h':
	  custom_header = optarg;
	  standard_header = FALSE;
	  break;
	case 'i':
	  if (optarg)
	    getoptarg (optarg, 'i', &output_tab_char,
		       &chars_per_output_tab);
	  /* Could check tab width > 0. */
	  tabify_output = TRUE;
	  break;
	case 'j':
	  join_lines = TRUE;
	  break;
	case 'l':
	  {
	    long int tmp_long;
	    if (xstrtol (optarg, NULL, 10, &tmp_long, NULL) != LONGINT_OK
		|| tmp_long <= 0 || tmp_long > INT_MAX)
	      {
		error (EXIT_FAILURE, 0,
		       _("`-l PAGE_LENGTH' invalid number of lines: `%s'"),
		       optarg);
	      }
	    lines_per_page = (int) tmp_long;
	    break;
	  }
	case 'm':
	  parallel_files = TRUE;
	  storing_columns = FALSE;
	  break;
	case 'n':
	  numbered_lines = TRUE;
	  if (optarg)
	    getoptarg (optarg, 'n', &number_separator,
		       &chars_per_number);
	  break;
	case 'N':
	  skip_count = FALSE;
	  {
	    long int tmp_long;
	    if (xstrtol (optarg, NULL, 10, &tmp_long, NULL) != LONGINT_OK
		|| tmp_long > INT_MAX)
	      {
		error (EXIT_FAILURE, 0,
		       _("`-N NUMBER' invalid starting line number: `%s'"),
		       optarg);
	      }
	    start_line_num = (int) tmp_long;
	    break;
	  }
	case 'o':
	  {
	    long int tmp_long;
	    if (xstrtol (optarg, NULL, 10, &tmp_long, NULL) != LONGINT_OK
		|| tmp_long <= 0 || tmp_long > INT_MAX)
	      error (EXIT_FAILURE, 0,
		     _("`-o MARGIN' invalid line offset: `%s'"), optarg);
	    chars_per_margin = (int) tmp_long;
	    break;
	  }
	case 'r':
	  ignore_failed_opens = TRUE;
	  break;
	case 's':
	  use_col_separator = TRUE;
	  if (optarg)
	    separator_string (optarg);
	  break;
	case 't':
	  extremities = FALSE;
	  keep_FF = TRUE;
	  break;
	case 'T':
	  extremities = FALSE;
	  keep_FF = FALSE;
	  break;
	case 'v':
	  use_esc_sequence = TRUE;
	  break;
	case 'w':
	  truncate_lines = TRUE;
	  {
	    long int tmp_long;
	    if (xstrtol (optarg, NULL, 10, &tmp_long, NULL) != LONGINT_OK
		|| tmp_long <= 0 || tmp_long > INT_MAX)
	      error (EXIT_FAILURE, 0,
		 _("`-w PAGE_WIDTH' invalid column number: `%s'"), optarg);
	    chars_per_line = (int) tmp_long;
	    break;
	  }
	default:
	  usage (2);
	  break;
	}
    }

  if (show_version)
    {
      printf ("pr (%s) %s\n", GNU_PACKAGE, VERSION);
      exit (EXIT_SUCCESS);
    }

  if (show_help)
    usage (0);

  if (parallel_files && explicit_columns)
    error (EXIT_FAILURE, 0,
	 _("Cannot specify number of columns when printing in parallel."));

  if (parallel_files && print_across_flag)
    error (EXIT_FAILURE, 0,
       _("Cannot specify both printing across and printing in parallel."));

  for (; optind < argc; optind++)
    {
      file_names[n_files++] = argv[optind];
    }

  if (n_files == 0)
    {
      /* No file arguments specified;  read from standard input.  */
      print_files (0, (char **) 0);
    }
  else
    {
      if (parallel_files)
	print_files (n_files, file_names);
      else
	{
	  int i;
	  for (i = 0; i < n_files; i++)
	    print_files (1, &file_names[i]);
	}
    }

  cleanup ();

  if (have_read_stdin && fclose (stdin) == EOF)
    error (EXIT_FAILURE, errno, _("standard input"));
  if (ferror (stdout) || fclose (stdout) == EOF)
    error (EXIT_FAILURE, errno, _("write error"));
  if (failed_opens > 0)
    exit (EXIT_FAILURE);
  exit (EXIT_SUCCESS);
}

/* Parse options of the form -scNNN.

   Example: -nck, where 'n' is the option, c is the optional number
   separator, and k is the optional width of the field used when printing
   a number. */

static void
getoptarg (char *arg, char switch_char, char *character, int *number)
{
  if (!ISDIGIT (*arg))
    *character = *arg++;
  if (*arg)
    {
      long int tmp_long;
      if (xstrtol (arg, NULL, 10, &tmp_long, NULL) != LONGINT_OK
	  || tmp_long <= 0 || tmp_long > INT_MAX)
	{
	  error (0, 0,
		 _("`-%c' extra characters or invalid number in the argument: `%s'"),
		 switch_char, arg);
	  usage (2);
	}
      *number = (int) tmp_long;
    }
}

/* Set parameters related to formatting. */

static void
init_parameters (int number_of_files)
{
  int chars_used_by_number = 0;

  if (use_form_feed)
    {
      lines_per_header = 3;
      lines_per_footer = 0;
    }

  lines_per_body = lines_per_page - lines_per_header - lines_per_footer;
  if (lines_per_body <= 0)
    {
      extremities = FALSE;
      keep_FF = TRUE;
    }
  if (extremities == FALSE)
    lines_per_body = lines_per_page;

  if (double_space)
    lines_per_body = lines_per_body / 2;

  /* If input is stdin, cannot print parallel files.  BSD dumps core
     on this. */
  if (number_of_files == 0)
    parallel_files = FALSE;

  if (parallel_files)
    columns = number_of_files;

  /* One file, multi columns down: -b option is set to get a consistent
     formulation with "FF set by hand" in input files. */
  if (storing_columns)
    balance_columns = TRUE;

  /* Tabification is assumed for multiple columns. */
  if (columns > 1)
    {
      if (!use_col_separator)
	{
	  col_sep_string = column_separator;
	  col_sep_length = 1;
	  use_col_separator = TRUE;
	}

      truncate_lines = TRUE;
      untabify_input = TRUE;
      tabify_output = TRUE;
    }
  else
    storing_columns = FALSE;

  /* -j dominates -w in any case */
  if (join_lines)
    truncate_lines = FALSE;

  if (numbered_lines)
    {
      line_count = start_line_num;
      if (number_separator == input_tab_char)
	{
	  number_width = chars_per_number +
	    tab_width (chars_per_input_tab,
		       (chars_per_margin + chars_per_number));
	}
      else
	number_width = chars_per_number + 1;
      /* The number is part of the column width unless we are
         printing files in parallel. */
      if (parallel_files)
	chars_used_by_number = number_width;
    }

  chars_per_column = (chars_per_line - chars_used_by_number -
		      (columns - 1) * col_sep_length) / columns;

  if (chars_per_column < 1)
    error (EXIT_FAILURE, 0, _("page width too narrow"));

  if (numbered_lines)
    {
      if (number_buff != (char *) 0)
	free (number_buff);
      number_buff = (char *) xmalloc (2 * chars_per_number);
    }

  /* Pick the maximum between the tab width and the width of an
     escape sequence. */
  if (clump_buff != (int *) 0)
    free (clump_buff);
  clump_buff = (int *) xmalloc ((chars_per_input_tab > 4
				 ? chars_per_input_tab : 4) * sizeof (int));
}

/* Open the necessary files,
   maintaining a COLUMN structure for each column.

   With multiple files, each column p has a different p->fp.
   With single files, each column p has the same p->fp.
   Return 1 if (number_of_files > 0) and no files can be opened,
   0 otherwise.

   With each column/file p, p->full_page_printed is initialized,
   see also open_file.  */

static int
init_fps (int number_of_files, char **av)
{
  int i, files_left;
  COLUMN *p;
  FILE *firstfp;
  char *firstname;

  total_files = 0;

  if (column_vector != NULLCOL)
    free ((char *) column_vector);
  column_vector = (COLUMN *) xmalloc (columns * sizeof (COLUMN));

  if (parallel_files)
    {
      files_left = number_of_files;
      for (p = column_vector; files_left--; ++p, ++av)
	{
	  if (open_file (*av, p) == 0)
	    {
	      --p;
	      --columns;
	    }
	}
      if (columns == 0)
	return 1;
      init_header ("", -1);
    }
  else
    {
      p = column_vector;
      if (number_of_files > 0)
	{
	  if (open_file (*av, p) == 0)
	    return 1;
	  init_header (*av, fileno (p->fp));
	}
      else
	{
	  p->name = _("standard input");
	  p->fp = stdin;
	  have_read_stdin = TRUE;
	  p->status = OPEN;
	  p->full_page_printed = FALSE;
	  ++total_files;
	  init_header ("", -1);
	}

      firstname = p->name;
      firstfp = p->fp;
      for (i = columns - 1, ++p; i; --i, ++p)
	{
	  p->name = firstname;
	  p->fp = firstfp;
	  p->status = OPEN;
	  p->full_page_printed = FALSE;
	}
    }
  files_ready_to_read = total_files;
  return 0;
}

/* Determine print_func and char_func, the functions
   used by each column for printing and/or storing.

   Determine the horizontal position desired when we begin
   printing a column (p->start_position). */

static void
init_funcs (void)
{
  int i, h, h_next;
  COLUMN *p;

  h = chars_per_margin;

  if (!truncate_lines)
    h_next = ANYWHERE;
  else
    {
      /* When numbering lines of parallel files, we enlarge the
         first column to accomodate the number.  Looks better than
         the Sys V approach. */
      if (parallel_files && numbered_lines)
	h_next = h + chars_per_column + number_width;
      else
	h_next = h + chars_per_column;
    }

  /* Enlarge p->start_position of first column to use the same form of
     padding_not_printed with all columns. */
  h = h + col_sep_length;

  /* This loop takes care of all but the rightmost column. */

  for (p = column_vector, i = 1; i < columns; ++p, ++i)
    {
      if (storing_columns)	/* One file, multi columns down. */
	{
	  p->char_func = store_char;
	  p->print_func = print_stored;
	}
      else
	/* One file, multi columns across; or parallel files.  */
	{
	  p->char_func = print_char;
	  p->print_func = read_line;
	}

      /* Number only the first column when printing files in
         parallel. */
      p->numbered = numbered_lines && (!parallel_files || i == 1);
      p->start_position = h;

      /* If we don't truncate lines, all start_positions are
         ANYWHERE, except the first column's start_position when
         using a margin. */

      if (!truncate_lines)
	{
	  h = ANYWHERE;
	  h_next = ANYWHERE;
	}
      else
	{
	  h = h_next + col_sep_length;
	  h_next = h + chars_per_column;
	}
    }

  /* The rightmost column.

     Doesn't need to be stored unless we intend to balance
     columns on the last page. */
  if (storing_columns && balance_columns)
    {
      p->char_func = store_char;
      p->print_func = print_stored;
    }
  else
    {
      p->char_func = print_char;
      p->print_func = read_line;
    }

  p->numbered = numbered_lines && (!parallel_files || i == 1);
  p->start_position = h;
}

/* Open a file.  Return nonzero if successful, zero if failed.

   With each file p, p->full_page_printed is initialized,
   see also init_fps. */

static int
open_file (char *name, COLUMN *p)
{
  if (!strcmp (name, "-"))
    {
      p->name = _("standard input");
      p->fp = stdin;
      have_read_stdin = 1;
    }
  else
    {
      p->name = name;
      p->fp = fopen (name, "r");
    }
  if (p->fp == NULL)
    {
      ++failed_opens;
      if (!ignore_failed_opens)
	error (0, errno, "%s", name);
      return 0;
    }
  p->status = OPEN;
  p->full_page_printed = FALSE;
  ++total_files;
  return 1;
}

/* Close the file in P.

   If we aren't dealing with multiple files in parallel, we change
   the status of all columns in the column list to reflect the close. */

static void
close_file (COLUMN *p)
{
  COLUMN *q;
  int i;

  if (p->status == CLOSED)
    return;
  if (ferror (p->fp))
    error (EXIT_FAILURE, errno, "%s", p->name);
  if (p->fp != stdin && fclose (p->fp) == EOF)
    error (EXIT_FAILURE, errno, "%s", p->name);

  if (!parallel_files)
    {
      for (q = column_vector, i = columns; i; ++q, --i)
	{
	  q->status = CLOSED;
	  if (q->lines_stored == 0)
	    {
	      q->lines_to_print = 0;
	    }
	}
    }
  else
    {
      p->status = CLOSED;
      p->lines_to_print = 0;
    }

  --files_ready_to_read;
}

/* Put a file on hold until we start a new page,
   since we've hit a form feed.

   If we aren't dealing with parallel files, we must change the
   status of all columns in the column list. */

static void
hold_file (COLUMN *p)
{
  COLUMN *q;
  int i;

  if (!parallel_files)
    for (q = column_vector, i = columns; i; ++q, --i)
      {
	if (storing_columns)
	  q->status = FF_FOUND;
	else
	  q->status = ON_HOLD;
      }
  else
    p->status = ON_HOLD;

  p->lines_to_print = 0;
  --files_ready_to_read;
}

/* Undo hold_file -- go through the column list and change any
   ON_HOLD columns to OPEN.  Used at the end of each page. */

static void
reset_status (void)
{
  int i = columns;
  COLUMN *p;

  for (p = column_vector; i; --i, ++p)
    if (p->status == ON_HOLD)
      {
	p->status = OPEN;
	files_ready_to_read++;
      }

  if (storing_columns)
    files_ready_to_read = 1;
}

/* Print a single file, or multiple files in parallel.

   Set up the list of columns, opening the necessary files.
   Allocate space for storing columns, if necessary.
   Skip to first_page_number, if user has asked to skip leading pages.
   Determine which functions are appropriate to store/print lines
   in each column.
   Print the file(s). */

static void
print_files (int number_of_files, char **av)
{
  init_parameters (number_of_files);
  if (init_fps (number_of_files, av))
    return;
  if (storing_columns)
    init_store_cols ();

  if (first_page_number > 1)
    {
      if (!skip_to_page (first_page_number))
	return;
      else
	page_number = first_page_number;
    }
  else
    page_number = 1;

  init_funcs ();

  line_number = line_count;
  while (print_page ())
    ;
}

/* Estimate the number of characters taken up by a short format date and
   time:  "yy-mm-dd HH:MM"  and:  "Page NNNN". */
#define CHARS_FOR_DATE_AND_PAGE	23

/* Initialize header information.
   If DESC is non-negative, it is a file descriptor open to
   FILENAME for reading.

   Allocate space for a header string,
   Determine the time, insert file name or user-specified string.
   Make use of a centered header with left-hand-side truncation marked by
   a '*` in front, if necessary. */

static void
init_header (char *filename, int desc)
{
  int chars_per_middle, chars_free, lhs_blanks, rhs_blanks;
  char *f = filename;
  char *no_middle = "";
  char *header_text, *fmt, *t_buf;
  struct tm *tmptr;
  struct stat st;
  char *datim = "- Date/Time --";

  fmt = "%y-%m-%d %H:%M";	/* date/time short format */

  if (filename == 0)
    f = "";

  if (header != (char *) 0)
    free (header);
  header = (char *) xmalloc (chars_per_line + 1);

  if (!standard_header && *custom_header == '\0')
    sprintf (header, "%s", " ");	/* blank line header */
  else
    {
      /* If parallel files or standard input, use current time. */
      if (desc < 0 || !strcmp (filename, "-") || fstat (desc, &st))
	st.st_mtime = time ((time_t *) 0);

      {
	size_t t_buf_size = 15;
	t_buf = (char *) xmalloc (t_buf_size);
	tmptr = localtime (&st.st_mtime);
	strftime (t_buf, t_buf_size, fmt, tmptr);
      }

      chars_per_middle = chars_per_line - CHARS_FOR_DATE_AND_PAGE;
      if (chars_per_middle < 3)
	{
	  header_text = no_middle;	/* Nothing free for a heading */
	  lhs_blanks = 1;
	  rhs_blanks = 1;
	}
      else
	{
	  header_text = standard_header ? f : custom_header;
	  chars_free = chars_per_middle - (int) strlen (header_text);
	  if (chars_free > 1)
	    {
	      lhs_blanks = chars_free / 2;	/* text not truncated */
	      rhs_blanks = chars_free - lhs_blanks;
	    }
	  else
	    {			/* lhs truncation */
	      header_text = header_text - chars_free + 2;
	      *header_text = '*';
	      lhs_blanks = 1;
	      rhs_blanks = 1;
	    }
	}

      sprintf (header, _("%s%*s%s%*sPage"), (test_suite ? datim : t_buf),
	       lhs_blanks, " ", header_text, rhs_blanks, " ");
    }
}

/* Set things up for printing a page

   Scan through the columns ...
   Determine which are ready to print
   (i.e., which have lines stored or open files)
   Set p->lines_to_print appropriately
   (to p->lines_stored if we're storing, or lines_per_body
   if we're reading straight from the file)
   Keep track of this total so we know when to stop printing */

static void
init_page (void)
{
  int j;
  COLUMN *p;

  if (storing_columns)
    {
      store_columns ();
      for (j = columns - 1, p = column_vector; j; --j, ++p)
	{
	  p->lines_to_print = p->lines_stored;
	}

      /* Last column. */
      if (balance_columns)
	{
	  p->lines_to_print = p->lines_stored;
	}
      /* Since we're not balancing columns, we don't need to store
         the rightmost column.   Read it straight from the file. */
      else
	{
	  if (p->status == OPEN)
	    {
	      p->lines_to_print = lines_per_body;
	    }
	  else
	    p->lines_to_print = 0;
	}
    }
  else
    for (j = columns, p = column_vector; j; --j, ++p)
      if (p->status == OPEN)
	{
	  p->lines_to_print = lines_per_body;
	}
      else
	p->lines_to_print = 0;
}

/* Align empty columns and print separators.
   Empty columns will be formed by files with status ON_HOLD or CLOSED
   when printing multiple files in parallel. */

static void
align_column (COLUMN *p)
{
  padding_not_printed = p->start_position;
  if (padding_not_printed - col_sep_length > 0)
    {
      pad_across_to (padding_not_printed - col_sep_length);
      padding_not_printed = ANYWHERE;
    }

  if (use_col_separator)
    print_sep_string ();

  if (p->numbered)
    number (p);
}

/* Print one page.

   As long as there are lines left on the page and columns ready to print,
   Scan across the column list
   if the column has stored lines or the file is open
   pad to the appropriate spot
   print the column
   pad the remainder of the page with \n or \f as requested
   reset the status of all files -- any files which where on hold because
   of formfeeds are now put back into the lineup. */

static int
print_page (void)
{
  int j;
  int lines_left_on_page;
  COLUMN *p;

  /* Used as an accumulator (with | operator) of successive values of
     pad_vertically.  The trick is to set pad_vertically
     to zero before each run through the inner loop, then after that
     loop, it tells us whether a line was actually printed (whether a
     newline needs to be output -- or two for double spacing).  But those
     values have to be accumulated (in pv) so we can invoke pad_down
     properly after the outer loop completes. */
  int pv;

  init_page ();

  if (cols_ready_to_print () == 0)
    return FALSE;

  if (extremities)
    print_a_header = TRUE;

  /* Don't pad unless we know a page was printed. */
  pad_vertically = FALSE;
  pv = FALSE;

  lines_left_on_page = lines_per_body;
  if (double_space)
    lines_left_on_page *= 2;

  while (lines_left_on_page > 0 && cols_ready_to_print () > 0)
    {
      output_position = 0;
      spaces_not_printed = 0;
      separators_not_printed = 0;
      pad_vertically = FALSE;
      align_empty_cols = FALSE;
      empty_line = TRUE;

      for (j = 1, p = column_vector; j <= columns; ++j, ++p)
	{
	  input_position = 0;
	  if (p->lines_to_print > 0 || p->status == FF_FOUND)
	    {
	      FF_only = FALSE;
	      padding_not_printed = p->start_position;
	      if (!(p->print_func) (p))
		read_rest_of_line (p);
	      pv |= pad_vertically;

	      --p->lines_to_print;
	      if (p->lines_to_print <= 0)
		{
		  if (cols_ready_to_print () <= 0)
		    break;
		}

	      /* File p changed its status to ON_HOLD or CLOSED */
	      if (parallel_files && p->status != OPEN)
		{
		  if (empty_line)
		    align_empty_cols = TRUE;
		  else if (p->status == CLOSED ||
			   (p->status == ON_HOLD && FF_only))
		    align_column (p);
		}
	    }
	  else if (parallel_files)
	    {
	      /* File status ON_HOLD or CLOSED */
	      if (empty_line)
		align_empty_cols = TRUE;
	      else
		align_column (p);
	    }

	  /* We need it also with an empty column */
	  if (use_col_separator)
	    ++separators_not_printed;
	}

      if (pad_vertically)
	{
	  putchar ('\n');
	  --lines_left_on_page;
	}

      if (double_space && pv && extremities)
	{
	  putchar ('\n');
	  --lines_left_on_page;
	}
    }

  if (lines_left_on_page == 0)
    for (j = 1, p = column_vector; j <= columns; ++j, ++p)
      if (p->status == OPEN)
	p->full_page_printed = TRUE;

  pad_vertically = pv;

  if (pad_vertically && extremities)
    pad_down (lines_left_on_page + lines_per_footer);
  else if (keep_FF && print_a_FF)
    {
      putchar ('\f');
      print_a_FF = FALSE;
    }

  if (last_page_number && page_number > last_page_number)
    return FALSE;		/* Stop printing with LAST_PAGE */

  reset_status ();		/* Change ON_HOLD to OPEN. */

  return TRUE;			/* More pages to go. */
}

/* Allocate space for storing columns.

   This is necessary when printing multiple columns from a single file.
   Lines are stored consecutively in buff, separated by '\0'.

   The following doesn't apply any longer - any tuning possible?
   (We can't use a fixed offset since with the '-s' flag lines aren't
   truncated.)

   We maintain a list (line_vector) of pointers to the beginnings
   of lines in buff.  We allocate one more than the number of lines
   because the last entry tells us the index of the last character,
   which we need to know in order to print the last line in buff. */

static void
init_store_cols (void)
{
  int total_lines = lines_per_body * columns;
  int chars_if_truncate = total_lines * (chars_per_column + 1);

  if (line_vector != (int *) 0)
    free ((int *) line_vector);
  line_vector = (int *) xmalloc ((total_lines + 1) * sizeof (int *));

  if (end_vector != (int *) 0)
    free ((int *) end_vector);
  end_vector = (int *) xmalloc (total_lines * sizeof (int *));

  if (buff != (char *) 0)
    free (buff);
  buff_allocated = use_col_separator ? 2 * chars_if_truncate
    : chars_if_truncate;	/* Tune this. */
  buff = (char *) xmalloc (buff_allocated);
}

/* Store all but the rightmost column.
   (Used when printing a single file in multiple downward columns)

   For each column
   set p->current_line to be the index in line_vector of the
   first line in the column
   For each line in the column
   store the line in buff
   add to line_vector the index of the line's first char
   buff_start is the index in buff of the first character in the
   current line. */

static void
store_columns (void)
{
  int i, j;
  int line = 0;
  int buff_start;
  int last_col;			/* The rightmost column which will be saved in buff */
  COLUMN *p;

  buff_current = 0;
  buff_start = 0;

  if (balance_columns)
    last_col = columns;
  else
    last_col = columns - 1;

  for (i = 1, p = column_vector; i <= last_col; ++i, ++p)
    p->lines_stored = 0;

  for (i = 1, p = column_vector; i <= last_col && files_ready_to_read;
       ++i, ++p)
    {
      p->current_line = line;
      for (j = lines_per_body; j && files_ready_to_read; --j)

	if (p->status == OPEN)	/* Redundant.  Clean up. */
	  {
	    input_position = 0;

	    if (!read_line (p))
	      read_rest_of_line (p);

	    if (p->status == OPEN
		|| buff_start != buff_current)
	      {
		++p->lines_stored;
		line_vector[line] = buff_start;
		end_vector[line++] = input_position;
		buff_start = buff_current;
	      }
	  }
    }

  /* Keep track of the location of the last char in buff. */
  line_vector[line] = buff_start;

  if (balance_columns && p->lines_stored != lines_per_body)
    balance (line);
}

static void
balance (int total_stored)
{
  COLUMN *p;
  int i, lines;
  int first_line = 0;

  for (i = 1, p = column_vector; i <= columns; ++i, ++p)
    {
      lines = total_stored / columns;
      if (i <= total_stored % columns)
	++lines;

      p->lines_stored = lines;
      p->current_line = first_line;

      first_line += lines;
    }
}

/* Store a character in the buffer. */

static void
store_char (int c)
{
  if (buff_current >= buff_allocated)
    {
      /* May be too generous. */
      buff_allocated = 2 * buff_allocated;
      buff = (char *) xrealloc (buff, buff_allocated * sizeof (char));
    }
  buff[buff_current++] = (char) c;
}

static void
number (COLUMN *p)
{
  int i;
  char *s;

  sprintf (number_buff, "%*d", chars_per_number, line_number++);
  s = number_buff;
  for (i = chars_per_number; i > 0; i--)
    (p->char_func) ((int) *s++);

  if (number_separator == input_tab_char)
    {
      i = number_width - chars_per_number;
      while (i-- > 0)
	(p->char_func) ((int) ' ');
    }
  else
    (p->char_func) ((int) number_separator);

  if (truncate_lines && !parallel_files)
    input_position += number_width;
}

/* Print (or store) padding until the current horizontal position
   is position. */

static void
pad_across_to (int position)
{
  register int h = output_position;

  if (tabify_output)
    spaces_not_printed = position - output_position;
  else
    {
      while (++h <= position)
	putchar (' ');
      output_position = position;
    }
}

/* Pad to the bottom of the page.

   If the user has requested a formfeed, use one.
   Otherwise, use newlines. */

static void
pad_down (int lines)
{
  register int i;

  if (use_form_feed)
    putchar ('\f');
  else
    for (i = lines; i; --i)
      putchar ('\n');
}

/* Read the rest of the line.

   Read from the current column's file until an end of line is
   hit.  Used when we've truncated a line and we no longer need
   to print or store its characters. */

static void
read_rest_of_line (COLUMN *p)
{
  register int c;
  FILE *f = p->fp;

  while ((c = getc (f)) != '\n')
    {
      if (c == '\f')
	{
	  if ((c = getc (f)) != '\n')
	    ungetc (c, f);
	  if (keep_FF)
	    print_a_FF = TRUE;
	  hold_file (p);
	  break;
	}
      else if (c == EOF)
	{
	  close_file (p);
	  break;
	}
    }
}

/* Read a line with skip_to_page.

   Read from the current column's file until an end of line is
   hit.  Used when we read full lines to skip pages.
   With skip_to_page we have to check for FF-coincidence which is done
   in function read_line otherwise.
   Count lines of skipped pages to find the line number of 1st page
   printed relative to 1st line of input file (start_line_num). */

static void
skip_read (COLUMN *p, int column_number)
{
  register int c;
  FILE *f = p->fp;
  int i, single_ff = FALSE;
  COLUMN *q;

  /* Read 1st character in a line or any character succeeding a FF */
  if ((c = getc (f)) == '\f' && p->full_page_printed)
    /* A FF-coincidence with a previous full_page_printed.
       To avoid an additional empty page, eliminate the FF */
    if ((c = getc (f)) == '\n')
      c = getc (f);

  p->full_page_printed = FALSE;

  /* 1st character a FF means a single FF without any printable
     characters. Don't count it as a line with -n option. */
  if (c == '\f')
    single_ff = TRUE;

  /* Preparing for a FF-coincidence: Maybe we finish that page
     without a FF found */
  if (last_line)
    p->full_page_printed = TRUE;

  while (c != '\n')
    {
      if (c == '\f')
	{
	  /* No FF-coincidence possible,
	     no catching up of a FF-coincidence with next page */
	  if (last_line)
	    {
	      if (!parallel_files)
		for (q = column_vector, i = columns; i; ++q, --i)
		  q->full_page_printed = FALSE;
	      else
		p->full_page_printed = FALSE;
	    }

	  if ((c = getc (f)) != '\n')
	    ungetc (c, f);
	  hold_file (p);
	  break;
	}
      else if (c == EOF)
	{
	  close_file (p);
	  break;
	}
      c = getc (f);
    }

  if (skip_count)
    if ((!parallel_files || column_number == 1) && !single_ff)
      ++line_count;
}

/* If we're tabifying output,

   When print_char encounters white space it keeps track
   of our desired horizontal position and delays printing
   until this function is called. */

static void
print_white_space (void)
{
  register int h_new;
  register int h_old = output_position;
  register int goal = h_old + spaces_not_printed;

  while (goal - h_old > 1
	 && (h_new = pos_after_tab (chars_per_output_tab, h_old)) <= goal)
    {
      putchar (output_tab_char);
      h_old = h_new;
    }
  while (++h_old <= goal)
    putchar (' ');

  output_position = goal;
  spaces_not_printed = 0;
}

/* Print column separators.

   We keep a count until we know that we'll be printing a line,
   then print_sep_string() is called. */

static void
print_sep_string ()
{
  char *s;
  int l = col_sep_length;

  s = col_sep_string;

  if (spaces_not_printed > 0)
    print_white_space ();

  for (; separators_not_printed > 0; --separators_not_printed)
    {
      while (l-- > 0)
	putchar (*s++);
      output_position += col_sep_length;
    }
}

/* Print (or store, depending on p->char_func) a clump of N
   characters. */

static void
print_clump (COLUMN *p, int n, int *clump)
{
  while (n--)
    (p->char_func) (*clump++);
}

/* Print a character.

   Update the following comment: process-char hasn't been used any
   longer.
   If we're tabifying, all tabs have been converted to spaces by
   process_char().  Keep a count of consecutive spaces, and when
   a nonspace is encountered, call print_white_space() to print the
   required number of tabs and spaces. */

static void
print_char (int c)
{
  if (tabify_output)
    {
      if (c == ' ')
	{
	  ++spaces_not_printed;
	  return;
	}
      else if (spaces_not_printed > 0)
	print_white_space ();

      /* Nonprintables are assumed to have width 0, except '\b'. */
      if (!ISPRINT (c))
	{
	  if (c == '\b')
	    --output_position;
	}
      else
	++output_position;
    }
  putchar (c);
}

/* Skip to page PAGE before printing. */

static int
skip_to_page (int page)
{
  int n, i, j;
  COLUMN *p;

  for (n = 1; n < page; ++n)
    {
      for (i = 1; i < lines_per_body; ++i)
	{
	  for (j = 1, p = column_vector; j <= columns; ++j, ++p)
	    if (p->status == OPEN)
	      skip_read (p, j);
	}
      last_line = TRUE;
      for (j = 1, p = column_vector; j <= columns; ++j, ++p)
	if (p->status == OPEN)
	  skip_read (p, j);

      if (storing_columns)	/* change FF_FOUND to ON_HOLD */
	for (j = 1, p = column_vector; j <= columns; ++j, ++p)
	  p->status = ON_HOLD;

      reset_status ();
      last_line = FALSE;
    }
  return files_ready_to_read > 0;
}

/* Print a header.

   Formfeeds are assumed to use up two lines at the beginning of
   the page. */

static void
print_header (void)
{
  if (!use_form_feed)
    fprintf (stdout, "\n\n");

  output_position = 0;
  pad_across_to (chars_per_margin);
  print_white_space ();

  if (!standard_header && *custom_header == '\0')
    fprintf (stdout, "%s\n\n\n", header);
  else
    fprintf (stdout, "%s%5d\n\n\n", header, page_number++);

  print_a_header = FALSE;
  output_position = 0;
}

/* Print (or store, if p->char_func is store_char()) a line.

   Read a character to determine whether we have a line or not.
   (We may hit EOF, \n, or \f)

   Once we know we have a line,
   set pad_vertically = TRUE, meaning it's safe
   to pad down at the end of the page, since we do have a page.
   print a header if needed.
   pad across to padding_not_printed if needed.
   print any separators which need to be printed.
   print a line number if it needs to be printed.

   Print the clump which corresponds to the first character.

   Enter a loop and keep printing until an end of line condition
   exists, or until we exceed chars_per_column.

   Return FALSE if we exceed chars_per_column before reading
   an end of line character, TRUE otherwise. */

static int
read_line (COLUMN *p)
{
  register int c, chars;
  int last_input_position;
  int j, k;
  COLUMN *q;

/* Suppress `used before initialized' warning. */
#ifdef lint
  chars = 0;
#endif

  /* read 1st character in each line or any character succeeding a FF: */
  c = getc (p->fp);

  last_input_position = input_position;

  if (c == '\f' && p->full_page_printed)
    if ((c = getc (p->fp)) == '\n')
      c = getc (p->fp);
  p->full_page_printed = FALSE;

  switch (c)
    {
    case '\f':
      if ((c = getc (p->fp)) != '\n')
	ungetc (c, p->fp);
      FF_only = TRUE;
      if (print_a_header && !storing_columns)
	{
	  pad_vertically = TRUE;
	  print_header ();
	}
      else if (keep_FF)
	print_a_FF = TRUE;
      hold_file (p);
      return TRUE;
    case EOF:
      close_file (p);
      return TRUE;
    case '\n':
      break;
    default:
      chars = char_to_clump (c);
    }

  if (truncate_lines && input_position > chars_per_column)
    {
      input_position = last_input_position;
      return FALSE;
    }

  if (p->char_func != store_char)
    {
      pad_vertically = TRUE;

      if (print_a_header && !storing_columns)
	print_header ();

      if (parallel_files && align_empty_cols)
	{
	  /* We have to align empty columns at the beginning of a line. */
	  k = separators_not_printed;
	  separators_not_printed = 0;
	  for (j = 1, q = column_vector; j <= k; ++j, ++q)
	    {
	      align_column (q);
	      separators_not_printed += 1;
	    }
	  padding_not_printed = p->start_position;
	  if (truncate_lines)
	    spaces_not_printed = chars_per_column;
	  else
	    spaces_not_printed = 0;
	  align_empty_cols = FALSE;
	}

      if (padding_not_printed - col_sep_length > 0)
	{
	  pad_across_to (padding_not_printed - col_sep_length);
	  padding_not_printed = ANYWHERE;
	}

      if (use_col_separator)
	print_sep_string ();
    }

  if (p->numbered)
    number (p);

  empty_line = FALSE;
  if (c == '\n')
    return TRUE;

  print_clump (p, chars, clump_buff);

  for (;;)
    {
      c = getc (p->fp);

      switch (c)
	{
	case '\n':
	  return TRUE;
	case '\f':
	  if ((c = getc (p->fp)) != '\n')
	    ungetc (c, p->fp);
	  if (keep_FF)
	    print_a_FF = TRUE;
	  hold_file (p);
	  return TRUE;
	case EOF:
	  close_file (p);
	  return TRUE;
	}

      last_input_position = input_position;
      chars = char_to_clump (c);
      if (truncate_lines && input_position > chars_per_column)
	{
	  input_position = last_input_position;
	  return FALSE;
	}

      print_clump (p, chars, clump_buff);
    }
}

/* Print a line from buff.

   If this function has been called, we know we have "something to
   print". But it remains to be seen whether we have a real text page
   or an empty page (a single form feed) with/without a header only.
   Therefore first we set pad_vertically to TRUE and print a header
   if necessary.
   If FF_FOUND and we are using -t|-T option we omit any newline by
   setting pad_vertically to FALSE (see print_page).
   Otherwise we pad across if necessary, print separators if necessary
   and text of COLUMN *p.

   Return TRUE, meaning there is no need to call read_rest_of_line. */

static int
print_stored (COLUMN *p)
{
  COLUMN *q;
  int i;

  int line = p->current_line++;
  register char *first = &buff[line_vector[line]];
  register char *last = &buff[line_vector[line + 1]];

  pad_vertically = TRUE;

  if (print_a_header)
    print_header ();

  if (p->status == FF_FOUND)
    {
      for (i = 1, q = column_vector; i <= columns; ++i, ++q)
	q->status = ON_HOLD;
      if (column_vector->lines_to_print <= 0)
	{
	  if (!extremities)
	    pad_vertically = FALSE;
	  return TRUE;		/* print a header only */
	}
    }

  if (padding_not_printed - col_sep_length > 0)
    {
      pad_across_to (padding_not_printed - col_sep_length);
      padding_not_printed = ANYWHERE;
    }

  if (use_col_separator)
    print_sep_string ();

  while (first != last)
    print_char (*first++);

  if (spaces_not_printed == 0)
    {
      output_position = p->start_position + end_vector[line];
      if (p->start_position - col_sep_length == chars_per_margin)
	output_position -= col_sep_length;
    }

  return TRUE;
}

/* Convert a character to the proper format and return the number of
   characters in the resulting clump.  Increment input_position by
   the width of the clump.

   Tabs are converted to clumps of spaces.
   Nonprintable characters may be converted to clumps of escape
   sequences or control prefixes.

   Note: the width of a clump is not necessarily equal to the number of
   characters in clump_buff.  (e.g, the width of '\b' is -1, while the
   number of characters is 1.) */

static int
char_to_clump (int c)
{
  register int *s = clump_buff;
  register int i;
  char esc_buff[4];
  int width;
  int chars;

  if (c == input_tab_char)
    {
      width = tab_width (chars_per_input_tab, input_position);

      if (untabify_input)
	{
	  for (i = width; i; --i)
	    *s++ = ' ';
	  chars = width;
	}
      else
	{
	  *s = c;
	  chars = 1;
	}

    }
  else if (!ISPRINT (c))
    {
      if (use_esc_sequence)
	{
	  width = 4;
	  chars = 4;
	  *s++ = '\\';
	  sprintf (esc_buff, "%03o", c);
	  for (i = 0; i <= 2; ++i)
	    *s++ = (int) esc_buff[i];
	}
      else if (use_cntrl_prefix)
	{
	  if (c < 0200)
	    {
	      width = 2;
	      chars = 2;
	      *s++ = '^';
	      *s++ = c ^ 0100;
	    }
	  else
	    {
	      width = 4;
	      chars = 4;
	      *s++ = '\\';
	      sprintf (esc_buff, "%03o", c);
	      for (i = 0; i <= 2; ++i)
		*s++ = (int) esc_buff[i];
	    }
	}
      else if (c == '\b')
	{
	  width = -1;
	  chars = 1;
	  *s = c;
	}
      else
	{
	  width = 0;
	  chars = 1;
	  *s = c;
	}
    }
  else
    {
      width = 1;
      chars = 1;
      *s = c;
    }

  input_position += width;
  return chars;
}

/* We've just printed some files and need to clean up things before
   looking for more options and printing the next batch of files.

   Free everything we've xmalloc'ed, except `header'. */

static void
cleanup (void)
{
  if (number_buff)
    free (number_buff);
  if (clump_buff)
    free (clump_buff);
  if (column_vector)
    free (column_vector);
  if (line_vector)
    free (line_vector);
  if (end_vector)
    free (end_vector);
  if (buff)
    free (buff);
}

/* Complain, print a usage message, and die. */

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
Paginate or columnate FILE(s) for printing.\n\
\n\
  +FIRST_PAGE[:LAST_PAGE]\n\
                    begin [stop] printing with page FIRST_[LAST_]PAGE\n\
  -COLUMN           produce COLUMN-column output and print columns down,\n\
                    unless -a is used. Balance number of lines in the\n\
                    columns on each page.\n\
  -a                print columns across rather than down, used together\n\
                    with -COLUMN\n\
  -c                use hat notation (^G) and octal backslash notation\n\
  -d                double space the output\n\
  -e[CHAR[WIDTH]]   expand input CHARs (TABs) to tab WIDTH (8)\n\
  -F, -f            use form feeds instead of newlines to separate pages\n\
                    (by a 3-line page header with -f or a 5-line header\n\
                    and trailer without -f)\n\
  -h HEADER         use a centered HEADER instead of filename in page headers\n\
                    with long headers left-hand-side truncation may occur\n\
                    -h \"\" prints a blank line. Don't use -h\"\"\n\
  -i[CHAR[WIDTH]]   replace spaces with CHARs (TABs) to tab WIDTH (8)\n\
  -j                merge full lines, turns off -w line truncation, no column\n\
                    alignment, -s[STRING] sets separators\n\
  -l PAGE_LENGTH    set the page length to PAGE_LENGTH (66) lines\n\
                    (default number of lines of text 56, with -f 63)\n\
  -m                print all files in parallel, one in each column,\n\
                    truncate lines, but join lines of full length with -j\n\
  -n[SEP[DIGITS]]   number lines, use DIGITS (5) digits, then SEP (TAB)\n\
                    default counting starts with 1st line of input file\n\
  -N NUMBER         start counting with NUMBER at 1st line of first\n\
                    page printed (see +FIRST_PAGE)\n\
  -o MARGIN         offset each line with MARGIN spaces (do not affect -w)\n\
  -r                inhibit warning when a file cannot be opened\n\
  -s[STRING]        separate columns by an optional STRING\n\
                    don't use -s \"STRING\" \n\
                    without -s: default sep. \'space\' used, same as -s\" \"\n\
                    -s only: no separator used, same as -s\"\" \n\
  -t                inhibit page headers and trailers\n\
  -T                inhibit page headers and trailers, eliminate any page\n\
                    layout by form feeds set in input files\n\
  -v                use octal backslash notation\n\
  -w PAGE_WIDTH     set page width to PAGE_WIDTH (72) columns, truncate\n\
                    lines (see also -j option)\n\
  --help            display this help and exit\n\
  --version         output version information and exit\n\
\n\
-T implied by -l nn when nn <= 10 or <= 3 with -f. With no FILE, or when\n\
FILE is -, read standard input.\n\
"));
      puts (_("\nReport bugs to textutils-bugs@gnu.ai.mit.edu"));
    }
  exit (status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
