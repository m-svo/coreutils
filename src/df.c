/* df - summarize free disk space
   Copyright (C) 91, 1995-1999 Free Software Foundation, Inc.

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

/* Written by David MacKenzie <djm@gnu.ai.mit.edu>.
   --human-readable and --megabyte options added by lm@sgi.com.
   --si and large file support added by eggert@twinsun.com.  */

#include <config.h>
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <getopt.h>
#include <assert.h>

#include "system.h"
#include "dirname.h"
#include "error.h"
#include "fsusage.h"
#include "human.h"
#include "mountlist.h"
#include "save-cwd.h"

/* The official name of this program (e.g., no `g' prefix).  */
#define PROGRAM_NAME "df"

#define AUTHORS \
  "Torbjorn Granlund, David MacKenzie, Larry McVoy, and Paul Eggert"

void strip_trailing_slashes ();
char *xstrdup ();
char *xgetcwd ();

/* Name this program was run with. */
char *program_name;

/* If nonzero, show inode information. */
static int inode_format;

/* If nonzero, show even filesystems with zero size or
   uninteresting types. */
static int show_all_fs;

/* If nonzero, show only local filesystems.  */
static int show_local_fs;

/* If nonzero, output data for each filesystem corresponding to a
   command line argument -- even if it's a dummy (automounter) entry.  */
static int show_listed_fs;

/* If positive, the units to use when printing sizes;
   if negative, the human-readable base.  */
static int output_block_size;

/* If nonzero, use the POSIX output format.  */
static int posix_format;

/* If nonzero, invoke the `sync' system call before getting any usage data.
   Using this option can make df very slow, especially with many or very
   busy disks.  Note that this may make a difference on some systems --
   SunOs4.1.3, for one.  It is *not* necessary on Linux.  */
static int require_sync = 0;

/* Nonzero if errors have occurred. */
static int exit_status;

/* A filesystem type to display. */

struct fs_type_list
{
  char *fs_name;
  struct fs_type_list *fs_next;
};

/* Linked list of filesystem types to display.
   If `fs_select_list' is NULL, list all types.
   This table is generated dynamically from command-line options,
   rather than hardcoding into the program what it thinks are the
   valid filesystem types; let the user specify any filesystem type
   they want to, and if there are any filesystems of that type, they
   will be shown.

   Some filesystem types:
   4.2 4.3 ufs nfs swap ignore io vm efs dbg */

static struct fs_type_list *fs_select_list;

/* Linked list of filesystem types to omit.
   If the list is empty, don't exclude any types.  */

static struct fs_type_list *fs_exclude_list;

/* Linked list of mounted filesystems. */
static struct mount_entry *mount_list;

/* If nonzero, print filesystem type as well.  */
static int print_type;

static struct option const long_options[] =
{
  {"all", no_argument, NULL, 'a'},
  {"block-size", required_argument, NULL, CHAR_MAX + 3},
  {"inodes", no_argument, NULL, 'i'},
  {"human-readable", no_argument, NULL, 'h'},
  {"si", no_argument, NULL, 'H'},
  {"kilobytes", no_argument, NULL, 'k'},
  {"local", no_argument, NULL, 'l'},
  {"megabytes", no_argument, NULL, 'm'},
  {"portability", no_argument, NULL, 'P'},
  {"print-type", no_argument, NULL, 'T'},
  {"sync", no_argument, NULL, CHAR_MAX + 1},
  {"no-sync", no_argument, NULL, CHAR_MAX + 2},
  {"type", required_argument, NULL, 't'},
  {"exclude-type", required_argument, NULL, 'x'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

static void
print_header (void)
{
  printf ("Filesystem ");

  if (print_type)
    printf ("   Type");
  else
    printf ("       ");

  if (inode_format)
    printf ("    Inodes   IUsed   IFree IUse%%");
  else if (output_block_size < 0)
    printf ("    Size  Used Avail Use%%");
  else
    {
      char buf[LONGEST_HUMAN_READABLE + 1];
      char *p = human_readable (output_block_size, buf, 1, -1024);

      /* Replace e.g. "1.0k" by "1k".  */
      size_t plen = strlen (p);
      if (3 <= plen && strncmp (p + plen - 3, ".0", 2) == 0)
	strcpy (p + plen - 3, p + plen - 1);

      printf (" %4s-blocks      Used Available Use%%", p);
    }

  printf (" Mounted on\n");
}

/* If FSTYPE is a type of filesystem that should be listed,
   return nonzero, else zero. */

static int
selected_fstype (const char *fstype)
{
  const struct fs_type_list *fsp;

  if (fs_select_list == NULL || fstype == NULL)
    return 1;
  for (fsp = fs_select_list; fsp; fsp = fsp->fs_next)
    if (STREQ (fstype, fsp->fs_name))
      return 1;
  return 0;
}

/* If FSTYPE is a type of filesystem that should be omitted,
   return nonzero, else zero. */

static int
excluded_fstype (const char *fstype)
{
  const struct fs_type_list *fsp;

  if (fs_exclude_list == NULL || fstype == NULL)
    return 0;
  for (fsp = fs_exclude_list; fsp; fsp = fsp->fs_next)
    if (STREQ (fstype, fsp->fs_name))
      return 1;
  return 0;
}

/* Like human_readable, except return "-" if the argument is -1.  */
static char *
df_readable (uintmax_t n, char *buf,
	     int from_block_size, int t_output_block_size)
{
  return (n == -1 ? "-"
	  : human_readable (n, buf, from_block_size, t_output_block_size));
}

/* Display a space listing for the disk device with absolute path DISK.
   If MOUNT_POINT is non-NULL, it is the path of the root of the
   filesystem on DISK.
   If FSTYPE is non-NULL, it is the type of the filesystem on DISK.
   If MOUNT_POINT is non-NULL, then DISK may be NULL -- certain systems may
   not be able to produce statistics in this case.
   ME_DUMMY and ME_REMOTE are the mount entry flags.  */

static void
show_dev (const char *disk, const char *mount_point, const char *fstype,
	  int me_dummy, int me_remote)
{
  struct fs_usage fsu;
  const char *stat_file;

  if (me_remote && show_local_fs)
    return;

  if (me_dummy && show_all_fs == 0 && !show_listed_fs)
    return;

  if (!selected_fstype (fstype) || excluded_fstype (fstype))
    return;

  /* If MOUNT_POINT is NULL, then the filesystem is not mounted, and this
     program reports on the filesystem that the special file is on.
     It would be better to report on the unmounted filesystem,
     but statfs doesn't do that on most systems.  */
  stat_file = mount_point ? mount_point : disk;

  if (get_fs_usage (stat_file, disk, &fsu))
    {
      error (0, errno, "%s", stat_file);
      exit_status = 1;
      return;
    }

  if (fsu.fsu_blocks == 0 && !show_all_fs && !show_listed_fs)
    return;

  if (! disk)
    disk = "-";			/* unknown */
  if (! fstype)
    fstype = "-";		/* unknown */

  /* df.c reserved 5 positions for fstype,
     but that does not suffice for type iso9660 */
  if (print_type)
    {
      int disk_name_len = (int) strlen (disk);
      int fstype_len = (int) strlen (fstype);
      if (disk_name_len + fstype_len + 2 < 20)
	printf ("%s%*s  ", disk, 18 - disk_name_len, fstype);
      else if (!posix_format)
	printf ("%s\n%18s  ", disk, fstype);
      else
	printf ("%s %s", disk, fstype);
    }
  else
    {
      if ((int) strlen (disk) > 20 && !posix_format)
	printf ("%s\n%20s", disk, "");
      else
	printf ("%-20s", disk);
    }

  if (inode_format)
    {
      char buf[3][LONGEST_HUMAN_READABLE + 1];
      double inodes_percent_used;
      uintmax_t inodes_used;
      int inode_units = output_block_size < 0 ? output_block_size : 1;

      if (fsu.fsu_files == -1 || fsu.fsu_files < fsu.fsu_ffree)
	{
	  inodes_used = -1;
	  inodes_percent_used = -1;
	}
      else
	{
	  inodes_used = fsu.fsu_files - fsu.fsu_ffree;
	  inodes_percent_used =
	    (fsu.fsu_files == 0 ? 0
	     : inodes_used * 100.0 / fsu.fsu_files);
	}

      printf (" %7s %7s %7s ",
	      df_readable (fsu.fsu_files, buf[0], 1, inode_units),
	      df_readable (inodes_used, buf[1], 1, inode_units),
	      df_readable (fsu.fsu_ffree, buf[2], 1, inode_units));

      if (inodes_percent_used < 0)
	printf ("   - ");
      else
	printf ("%4.0f%%", inodes_percent_used);
    }
  else
    {
      int w = output_block_size < 0 ? 5 : 9;
      char buf[2][LONGEST_HUMAN_READABLE + 1];
      char availbuf[LONGEST_HUMAN_READABLE + 2];
      char *avail;
      double blocks_percent_used;
      uintmax_t blocks_used;

      if (fsu.fsu_blocks == -1 || fsu.fsu_blocks < fsu.fsu_bfree)
	{
	  blocks_used = -1;
	  blocks_percent_used = -1;
	}
      else
	{
	  blocks_used = fsu.fsu_blocks - fsu.fsu_bfree;
	  blocks_percent_used =
	    ((fsu.fsu_bavail == -1
	      || blocks_used + fsu.fsu_bavail == 0
	      || (fsu.fsu_bavail_top_bit_set
	          ? blocks_used < - fsu.fsu_bavail
	          : fsu.fsu_bfree < fsu.fsu_bavail))
	     ? -1
	     : blocks_used * 100.0 / (blocks_used + fsu.fsu_bavail));
	}

      avail = df_readable ((fsu.fsu_bavail_top_bit_set
			    ? - fsu.fsu_bavail
			    : fsu.fsu_bavail),
			   availbuf + 1, fsu.fsu_blocksize,
			   output_block_size);

      if (fsu.fsu_bavail_top_bit_set)
	*--avail = '-';

      printf (" %*s %*s %*s ",
	      w, df_readable (fsu.fsu_blocks, buf[0], fsu.fsu_blocksize,
			      output_block_size),
	      w, df_readable (blocks_used, buf[1], fsu.fsu_blocksize,
			      output_block_size),
	      w, avail);

      if (blocks_percent_used < 0)
	printf ("  - ");
      else
	printf ("%3.0f%%", blocks_percent_used);
    }

  if (mount_point)
    {
#ifdef HIDE_AUTOMOUNT_PREFIX
      /* Don't print the first directory name in MOUNT_POINT if it's an
	 artifact of an automounter.  This is a bit too aggressive to be
	 the default.  */
      if (strncmp ("/auto/", mount_point, 6) == 0)
	mount_point += 5;
      else if (strncmp ("/tmp_mnt/", mount_point, 9) == 0)
	mount_point += 8;
#endif
      printf (" %s", mount_point);
    }
  putchar ('\n');
}

/* Identify the directory, if any, that device
   DISK is mounted on, and show its disk usage.  */

static void
show_disk (const char *disk)
{
  struct mount_entry *me;

  for (me = mount_list; me; me = me->me_next)
    if (STREQ (disk, me->me_devname))
      {
	show_dev (me->me_devname, me->me_mountdir, me->me_type,
		  me->me_dummy, me->me_remote);
	return;
      }
  /* No filesystem is mounted on DISK. */
  show_dev (disk, (char *) NULL, (char *) NULL, 0, 0);
}

/* Return the root mountpoint of the filesystem on which FILE exists, in
   malloced storage.  FILE_STAT should be the result of stating FILE.  */
static char *
find_mount_point (const char *file, const struct stat *file_stat)
{
  struct saved_cwd cwd;
  struct stat last_stat;
  char *mp = 0;			/* The malloced mount point path.  */

  if (save_cwd (&cwd))
    return NULL;

  if (S_ISDIR (file_stat->st_mode))
    /* FILE is a directory, so just chdir there directly.  */
    {
      last_stat = *file_stat;
      if (chdir (file) < 0)
	return NULL;
    }
  else
    /* FILE is some other kind of file, we need to use its directory.  */
    {
      int rv;
      char *tmp = xstrdup (file);
      char *dir;

      strip_trailing_slashes (tmp);
      dir = dir_name (tmp);
      free (tmp);
      rv = chdir (dir);
      free (dir);

      if (rv < 0)
	return NULL;

      if (stat (".", &last_stat) < 0)
	goto done;
    }

  /* Now walk up FILE's parents until we find another filesystem or /,
     chdiring as we go.  LAST_STAT holds stat information for the last place
     we visited.  */
  for (;;)
    {
      struct stat st;
      if (stat ("..", &st) < 0)
	goto done;
      if (st.st_dev != last_stat.st_dev || st.st_ino == last_stat.st_ino)
	/* cwd is the mount point.  */
	break;
      if (chdir ("..") < 0)
	goto done;
      last_stat = st;
    }

  /* Finally reached a mount point, see what it's called.  */
  mp = xgetcwd ();

done:
  /* Restore the original cwd.  */
  {
    int save_errno = errno;
    if (restore_cwd (&cwd, 0, mp))
      exit (1);			/* We're scrod.  */
    free_cwd (&cwd);
    errno = save_errno;
  }

  return mp;
}

/* Figure out which device file or directory POINT is mounted on
   and show its disk usage.
   STATP is the results of `stat' on POINT.  */
static void
show_point (const char *point, const struct stat *statp)
{
  struct stat disk_stats;
  struct mount_entry *me;
  struct mount_entry *matching_dummy = NULL;

  for (me = mount_list; me; me = me->me_next)
    {
      if (me->me_dev == (dev_t) -1)
	{
	  if (stat (me->me_mountdir, &disk_stats) == 0)
	    me->me_dev = disk_stats.st_dev;
	  else
	    {
	      error (0, errno, "%s", me->me_mountdir);
	      exit_status = 1;
	      /* So we won't try and fail repeatedly. */
	      me->me_dev = (dev_t) -2;
	    }
	}

      if (statp->st_dev == me->me_dev)
	{
	  /* Skip bogus mtab entries.  */
	  if (stat (me->me_mountdir, &disk_stats) != 0 ||
	      disk_stats.st_dev != me->me_dev)
	    continue;

	  /* Prefer non-dummy entries.  */
	  if (me->me_dummy)
	    {
	      matching_dummy = me;
	      continue;
	    }

	  show_dev (me->me_devname, me->me_mountdir, me->me_type,
		    me->me_dummy, me->me_remote);
	  return;
	}
    }

  if (matching_dummy)
    {
      show_dev (matching_dummy->me_devname, matching_dummy->me_mountdir,
		matching_dummy->me_type, 1, matching_dummy->me_remote);
      return;
    }

  /* We couldn't find the mount entry corresponding to POINT.  Go ahead and
     print as much info as we can; methods that require the device to be
     present will fail at a later point.  */
  {
    /* Find the actual mount point.  */
    char *mp = find_mount_point (point, statp);
    if (mp)
      {
	show_dev (0, mp, 0, 0, 0);
	free (mp);
      }
    else
      error (0, errno, "%s", point);
  }
}

/* Determine what kind of node PATH is and show the disk usage
   for it.  STATP is the results of `stat' on PATH.  */

static void
show_entry (const char *path, const struct stat *statp)
{
  if (S_ISBLK (statp->st_mode) || S_ISCHR (statp->st_mode))
    show_disk (path);
  else
    show_point (path, statp);
}

/* Show all mounted filesystems, except perhaps those that are of
   an unselected type or are empty. */

static void
show_all_entries (void)
{
  struct mount_entry *me;

  for (me = mount_list; me; me = me->me_next)
    show_dev (me->me_devname, me->me_mountdir, me->me_type,
	      me->me_dummy, me->me_remote);
}

/* Add FSTYPE to the list of filesystem types to display. */

static void
add_fs_type (const char *fstype)
{
  struct fs_type_list *fsp;

  fsp = (struct fs_type_list *) xmalloc (sizeof (struct fs_type_list));
  fsp->fs_name = (char *) fstype;
  fsp->fs_next = fs_select_list;
  fs_select_list = fsp;
}

/* Add FSTYPE to the list of filesystem types to be omitted. */

static void
add_excluded_fs_type (const char *fstype)
{
  struct fs_type_list *fsp;

  fsp = (struct fs_type_list *) xmalloc (sizeof (struct fs_type_list));
  fsp->fs_name = (char *) fstype;
  fsp->fs_next = fs_exclude_list;
  fs_exclude_list = fsp;
}

void
usage (int status)
{
  if (status != 0)
    fprintf (stderr, _("Try `%s --help' for more information.\n"),
	     program_name);
  else
    {
      printf (_("Usage: %s [OPTION]... [FILE]...\n"), program_name);
      printf (_("\
Show information about the filesystem on which each FILE resides,\n\
or all filesystems by default.\n\
\n\
  -a, --all             include filesystems having 0 blocks\n\
      --block-size=SIZE use SIZE-byte blocks\n\
  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n\
  -H, --si              likewise, but use powers of 1000 not 1024\n\
  -i, --inodes          list inode information instead of block usage\n\
  -k, --kilobytes       like --block-size=1024\n\
  -l, --local           limit listing to local filesystems\n\
  -m, --megabytes       like --block-size=1048576\n\
      --no-sync         do not invoke sync before getting usage info (default)\n\
  -P, --portability     use the POSIX output format\n\
      --sync            invoke sync before getting usage info\n\
  -t, --type=TYPE       limit listing to filesystems of type TYPE\n\
  -T, --print-type      print filesystem type\n\
  -x, --exclude-type=TYPE   limit listing to filesystems not of type TYPE\n\
  -v                    (ignored)\n\
      --help            display this help and exit\n\
      --version         output version information and exit\n\
"));
      puts (_("\nReport bugs to <bug-fileutils@gnu.org>."));
      close_stdout ();
    }
  exit (status);
}

int
main (int argc, char **argv)
{
  int c;
  struct stat *stats;

  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  fs_select_list = NULL;
  fs_exclude_list = NULL;
  inode_format = 0;
  show_all_fs = 0;
  show_listed_fs = 0;

  human_block_size (getenv ("DF_BLOCK_SIZE"), 0, &output_block_size);

  print_type = 0;
  posix_format = 0;
  exit_status = 0;

  while ((c = getopt_long (argc, argv, "aiF:hHklmPTt:vx:", long_options, NULL))
	 != -1)
    {
      switch (c)
	{
	case 0:			/* Long option. */
	  break;
	case 'a':
	  show_all_fs = 1;
	  break;
	case 'i':
	  inode_format = 1;
	  break;
	case 'h':
	  output_block_size = -1024;
	  break;
	case 'H':
	  output_block_size = -1000;
	  break;
	case 'k':
	  output_block_size = 1024;
	  break;
	case 'l':
	  show_local_fs = 1;
	  break;
	case 'm':
	  output_block_size = 1024 * 1024;
	  break;
	case 'T':
	  print_type = 1;
	  break;
	case 'P':
	  posix_format = 1;
	  break;
	case CHAR_MAX + 1:
	  require_sync = 1;
	  break;
	case CHAR_MAX + 2:
	  require_sync = 0;
	  break;

	case CHAR_MAX + 3:
	  human_block_size (optarg, 1, &output_block_size);
	  break;

	case 'F':
	  /* Accept -F as a synonym for -t for compatibility with Solaris.  */
	case 't':
	  add_fs_type (optarg);
	  break;

	case 'v':		/* For SysV compatibility. */
	  /* ignore */
	  break;
	case 'x':
	  add_excluded_fs_type (optarg);
	  break;

	case_GETOPT_HELP_CHAR;
	case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

	default:
	  usage (1);
	}
    }

  /* Fail if the same file system type was both selected and excluded.  */
  {
    int match = 0;
    struct fs_type_list *i;
    for (i = fs_select_list; i; i = i->fs_next)
      {
	struct fs_type_list *j;
	for (j = fs_exclude_list; j; j = j->fs_next)
	  {
	    if (STREQ (i->fs_name, j->fs_name))
	      {
		error (0, 0,
		       _("file system type `%s' both selected and excluded"),
		       i->fs_name);
		match = 1;
		break;
	      }
	  }
      }
    if (match)
      exit (1);
  }

  if (optind == argc)
    {
#ifdef lint
      /* Suppress `used before initialized' warning.  */
      stats = NULL;
#endif
    }
  else
    {
      int i;

      /* stat all the given entries to make sure they get automounted,
	 if necessary, before reading the filesystem table.  */
      stats = (struct stat *)
	xmalloc ((argc - optind) * sizeof (struct stat));
      for (i = optind; i < argc; ++i)
	if (stat (argv[i], &stats[i - optind]))
	  {
	    error (0, errno, "%s", argv[i]);
	    exit_status = 1;
	    argv[i] = NULL;
	  }
    }

  mount_list =
    read_filesystem_list ((fs_select_list != NULL
			   || fs_exclude_list != NULL
			   || print_type
			   || show_local_fs));

  if (mount_list == NULL)
    error (1, errno, _("cannot read table of mounted filesystems"));

  if (require_sync)
    sync ();

  if (optind == argc)
    {
      print_header ();
      show_all_entries ();
    }
  else
    {
      int i;

      /* Display explicitly requested empty filesystems. */
      show_listed_fs = 1;

      print_header ();
      for (i = optind; i < argc; ++i)
	if (argv[i])
	  show_entry (argv[i], &stats[i - optind]);
    }

  close_stdout ();
  exit (exit_status);
}
