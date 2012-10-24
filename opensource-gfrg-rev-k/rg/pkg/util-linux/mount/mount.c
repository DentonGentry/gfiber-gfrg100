/*
 * A mount(8) for Linux 0.99.
 * mount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * Wed Sep 14 22:43:00 1994: Mitchum DSouza
 * (mitch@mrc-applied-psychology.cambridge.ac.uk) added support for mounting
 * the "loop" device.
 *
 * Wed Sep 14 22:55:10 1994: Sander van Malssen (svm@kozmix.hacktic.nl)
 * added support for remounting readonly file systems readonly.
 *
 * Wed Feb  8 12:27:00 1995: Andries.Brouwer@cwi.nl fixed up error messages.
 * Sat Jun  3 20:44:38 1995: Patches from Andries.Brouwer@cwi.nl applied.
 * Tue Sep 26 22:38:20 1995: aeb@cwi.nl, many changes
 * Fri Feb 23 13:47:00 1996: aeb@cwi.nl, loop device related changes
 *
 * Since then, many changes - aeb.
 *
 * Wed Oct  1 23:55:28 1997: Dick Streefland <dick_streefland@tasking.com>
 * Implemented the "bg", "fg" and "retry" mount options for NFS.
 *
 * Tue Aug  4 15:54:31 1998: aeb@cwi.nl:
 * Open fd 0,1,2 so that printf's do not clobber /etc/mtab or so.
 * Mangle filenames with embedded spaces. Add ufsmagic. Add locking.
 * Avoid unnecessary error messages about /proc.
 * Improve support for noncanonical names in /etc/fstab.
 * Add support for volume labels and UUIDs.
 *
 * 1999-02-22 Arkadiusz Mi�kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 * 1999-03-21 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fixed strerr(errno) in gettext calls
 * 1999-07-05 Hirokazu Takahashi <h-takaha@sss.abk.nec.co.jp>
 * - fixed use of nouser option
 * 1999-09-09 Michael K. Johnson <johnsonm@redhat.com>
 * - added `owner' mount option
 * 2000-05-11 Mark A. Peloquin <peloquin@us.ibm.com>
 * - check_special_mountprog now returns correct status
 */

#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include "mount_constants.h"
#include "sundries.h"
#include "mntent.h"
#include "fstab.h"
#include "lomount.h"
#include "loop.h"
#include "linux_fs.h"		/* for BLKGETSIZE */
#include "mount_guess_fstype.h"
#include "mount_by_label.h"
#include "getusername.h"
#include "nls.h"
 
#include "libc_config.h"
 
#ifdef HAVE_LOCALE_H
	#include <locale.h>
#endif

#define DO_PS_FIDDLING

#ifdef DO_PS_FIDDLING
#include "setproctitle.h"
#endif

/* True for fake mount (-f).  */
static int fake = 0;

/* Don't write a entry in /etc/mtab (-n).  */
static int nomtab = 0;

/* True for explicit readonly (-r).  */
static int readonly = 0;

/* Nonzero for chatty (-v).  */
int verbose = 0;

/* Nonzero for sloppy (-s).  */
int sloppy = 0;

/* True for explicit read/write (-w).  */
static int readwrite = 0;

/* True for all mount (-a).  */
int all = 0;

/* True for fork() during all mount (-F).  */
static int optfork = 0;

/* Add volumelabel in a listing of mounted devices (-l). */
static int list_with_volumelabel = 0;

/* True if ruid != euid.  */
static int suid = 0;

/* Map from -o and fstab option strings to the flag argument to mount(2).  */
struct opt_map {
  const char *opt;		/* option name */
  int  skip;			/* skip in mtab option string */
  int  inv;			/* true if flag value should be inverted */
  int  mask;			/* flag mask value */
};

/* Custom mount options for our own purposes.  */
/* We can use the high-order 16 bits, since the mount call
   has MS_MGC_VAL there. */
#define MS_NOAUTO	0x80000000
#define MS_USERS	0x40000000
#define MS_USER		0x20000000
#define MS_OWNER	0x10000000
#define MS_LOOP		0x00010000

/* Options that we keep the mount system call from seeing.  */
#define MS_NOSYS	(MS_NOAUTO|MS_USERS|MS_USER|MS_LOOP)

/* Options that we keep from appearing in the options field in the mtab.  */
#define MS_NOMTAB	(MS_REMOUNT|MS_NOAUTO|MS_USERS|MS_USER)

/* Options that we make ordinary users have by default.  */
#define MS_SECURE	(MS_NOEXEC|MS_NOSUID|MS_NODEV)

/* Options that we make owner-mounted devices have by default */
#define MS_OWNERSECURE	(MS_NOSUID|MS_NODEV)

static const struct opt_map opt_map[] = {
  { "defaults",	0, 0, 0		},	/* default options */
  { "ro",	1, 0, MS_RDONLY	},	/* read-only */
  { "rw",	1, 1, MS_RDONLY	},	/* read-write */
  { "exec",	0, 1, MS_NOEXEC	},	/* permit execution of binaries */
  { "noexec",	0, 0, MS_NOEXEC	},	/* don't execute binaries */
  { "suid",	0, 1, MS_NOSUID	},	/* honor suid executables */
  { "nosuid",	0, 0, MS_NOSUID	},	/* don't honor suid executables */
  { "dev",	0, 1, MS_NODEV	},	/* interpret device files  */
  { "nodev",	0, 0, MS_NODEV	},	/* don't interpret devices */
  { "sync",	0, 0, MS_SYNCHRONOUS},	/* synchronous I/O */
  { "async",	0, 1, MS_SYNCHRONOUS},	/* asynchronous I/O */
  { "remount",  0, 0, MS_REMOUNT},      /* Alter flags of mounted FS */
  { "auto",	0, 1, MS_NOAUTO	},	/* Can be mounted using -a */
  { "noauto",	0, 0, MS_NOAUTO	},	/* Can  only be mounted explicitly */
  { "users",	0, 0, MS_USERS	},	/* Allow ordinary user to mount */
  { "nousers",	0, 1, MS_USERS	},	/* Forbid ordinary user to mount */
  { "user",	0, 0, MS_USER	},	/* Allow ordinary user to mount */
  { "nouser",	0, 1, MS_USER	},	/* Forbid ordinary user to mount */
  { "owner",	0, 0, MS_OWNER  },	/* Let the owner of the device mount */
  { "noowner",	0, 1, MS_OWNER  },	/* Device owner has no special privs */
  /* add new options here */
#ifdef MS_NOSUB
  { "sub",	0, 1, MS_NOSUB	},	/* allow submounts */
  { "nosub",	0, 0, MS_NOSUB	},	/* don't allow submounts */
#endif
#ifdef MS_SILENT
  { "quiet",	0, 0, MS_SILENT    },	/* be quiet  */
  { "loud",	0, 1, MS_SILENT    },	/* print out messages. */
#endif
#ifdef MS_MANDLOCK
  { "mand",	0, 0, MS_MANDLOCK },	/* Allow mandatory locks on this FS */
  { "nomand",	0, 1, MS_MANDLOCK },	/* Forbid mandatory locks on this FS */
#endif
  { "loop",	1, 0, MS_LOOP	},	/* use a loop device */
#ifdef MS_NOATIME
  { "atime",	0, 1, MS_NOATIME },	/* Update access time */
  { "noatime",	0, 0, MS_NOATIME },	/* Do not update access time */
#endif
#ifdef MS_NODIRATIME
  { "diratime",	0, 1, MS_NODIRATIME },	/* Update dir access times */
  { "nodiratime", 0, 0, MS_NODIRATIME },/* Do not update dir access times */
#endif
  { NULL,	0, 0, 0		}
};

static char *opt_loopdev, *opt_vfstype, *opt_offset, *opt_encryption,
  *opt_speed;

static struct string_opt_map {
  char *tag;
  int skip;
  char **valptr;
} string_opt_map[] = {
  { "loop=",	0, &opt_loopdev },
  { "vfs=",	1, &opt_vfstype },
  { "offset=",	0, &opt_offset },
  { "encryption=", 0, &opt_encryption },
  { "speed=", 0, &opt_speed },
  { NULL, 0, NULL }
};

static void
clear_string_opts(void) {
  struct string_opt_map *m;

  for (m = &string_opt_map[0]; m->tag; m++)
    *(m->valptr) = NULL;
}

static int
parse_string_opt(char *s) {
  struct string_opt_map *m;
  int lth;

  for (m = &string_opt_map[0]; m->tag; m++) {
    lth = strlen(m->tag);
    if (!strncmp(s, m->tag, lth)) {
      *(m->valptr) = xstrdup(s + lth);
      return 1;
    }
  }
  return 0;
}

int mount_quiet=0;

/* Report on a single mount.  */
static void
print_one (const struct mntentchn *mc) {
     if (mount_quiet)
	  return;
     printf ("%s on %s", mc->mnt_fsname, mc->mnt_dir);
     if (mc->mnt_type != NULL && *(mc->mnt_type) != '\0')
	  printf (" type %s", mc->mnt_type);
     if (mc->mnt_opts != NULL)
	  printf (" (%s)", mc->mnt_opts);
     if (list_with_volumelabel) {
	     const char *label;
	     label = get_volume_label_by_spec(mc->mnt_fsname);
	     if (label)
		     printf (" [%s]", label);
     }
     printf ("\n");
}

/* Report on everything in mtab (of the specified types if any).  */
static int
print_all (string_list types) {
     struct mntentchn *mc;

     for (mc = mtab_head()->nxt; mc; mc = mc->nxt) {
	  if (matching_type (mc->mnt_type, types))
	       print_one (mc);
     }
     exit (0);
}


/* Look for OPT in opt_map table and return mask value.  If OPT isn't found,
   tack it onto extra_opts (which is non-NULL).  */
static inline void
parse_opt (const char *opt, int *mask, char *extra_opts) {
  const struct opt_map *om;

  for (om = opt_map; om->opt != NULL; om++)
      if (streq (opt, om->opt)) {
	if (om->inv)
	  *mask &= ~om->mask;
	else
	  *mask |= om->mask;
	if ((om->mask == MS_USER || om->mask == MS_USERS)
	    && !om->inv)
	  *mask |= MS_SECURE;
	if ((om->mask == MS_OWNER) && !om->inv)
	  *mask |= MS_OWNERSECURE;
#ifdef MS_SILENT
        if (om->mask == MS_SILENT && om->inv)  {
          mount_quiet = 1;
          verbose = 0;
        }
#endif
	return;
      }
  if (*extra_opts)
    strcat(extra_opts, ",");
  strcat(extra_opts, opt);
}
  
/* Take -o options list and compute 4th and 5th args to mount(2).  flags
   gets the standard options and extra_opts anything we don't recognize.  */
static void
parse_opts (char *opts, int *flags, char **extra_opts) {
  char *opt;

  *flags = 0;
  *extra_opts = NULL;

  clear_string_opts();

  if (opts != NULL)
    {
      *extra_opts = xmalloc (strlen (opts) + 1); 
      **extra_opts = '\0';

      for (opt = strtok (opts, ","); opt; opt = strtok (NULL, ","))
	if (!parse_string_opt (opt))
	  parse_opt (opt, flags, *extra_opts);
    }

  if (readonly)
    *flags |= MS_RDONLY;
  if (readwrite)
    *flags &= ~MS_RDONLY;
}

/* Try to build a canonical options string.  */
static char *
fix_opts_string (int flags, const char *extra_opts, const char *user) {
  const struct opt_map *om;
  const struct string_opt_map *m;
  char *new_opts;

  new_opts = (flags & MS_RDONLY) ? "ro" : "rw";
  for (om = opt_map; om->opt != NULL; om++) {
      if (om->skip)
	continue;
      if (om->inv || !om->mask || (flags & om->mask) != om->mask)
	continue;
      new_opts = xstrconcat3(new_opts, ",", om->opt);
      flags &= ~om->mask;
  }
  for (m = &string_opt_map[0]; m->tag; m++) {
      if (!m->skip && *(m->valptr))
	   new_opts = xstrconcat4(new_opts, ",", m->tag, *(m->valptr));
  }
  if (extra_opts && *extra_opts) {
      new_opts = xstrconcat3(new_opts, ",", extra_opts);
  }
  if (user) {
      new_opts = xstrconcat3(new_opts, ",user=", user);
  }
  return new_opts;
}

static int
already (const char *spec, const char *node) {
    struct mntentchn *mc;
    int ret = 1;

    if ((mc = getmntfile(node)) != NULL)
        error (_("mount: according to mtab, %s is already mounted on %s"),
	       mc->mnt_fsname, node);
    else if ((mc = getmntfile(spec)) != NULL)
        error (_("mount: according to mtab, %s is mounted on %s"),
	       spec, mc->mnt_dir);
    else
        ret = 0;
    return ret;
}

/* Create mtab with a root entry.  */
static void
create_mtab (void) {
  struct mntentchn *fstab;
  struct mntent mnt;
  int flags;
  char *extra_opts;
  mntFILE *mfp;

  lock_mtab();

  mfp = my_setmntent (MOUNTED, "a+");
  if (mfp == NULL || mfp->mntent_fp == NULL) {
    int errsv = errno;
    die (EX_FILEIO, _("mount: can't open %s for writing: %s"),
	 MOUNTED, strerror (errsv));
  }

  /* Find the root entry by looking it up in fstab */
  if ((fstab = getfsfile ("/")) || (fstab = getfsfile ("root"))) {
      parse_opts (xstrdup (fstab->mnt_opts), &flags, &extra_opts);
      mnt.mnt_dir = "/";
      mnt.mnt_fsname = canonicalize (fstab->mnt_fsname);
      mnt.mnt_type = fstab->mnt_type;
      mnt.mnt_opts = fix_opts_string (flags, extra_opts, NULL);
      mnt.mnt_freq = mnt.mnt_passno = 0;

      if (my_addmntent (mfp, &mnt) == 1) {
        int errsv = errno;
	die (EX_FILEIO, _("mount: error writing %s: %s"),
	     MOUNTED, strerror (errsv));
      }
  }
  if (fchmod (fileno (mfp->mntent_fp), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0)
    if (errno != EROFS) {
      int errsv = errno;
      die (EX_FILEIO, _("mount: error changing mode of %s: %s"),
	   MOUNTED, strerror (errsv));
    }
  my_endmntent (mfp);

  unlock_mtab();
}

/* count successful mount system calls */
static int mountcount = 0;

/*
 * do_mount_syscall()
 *	Mount a single file system. Keep track of successes.
 * returns: 0: OK, -1: error in errno
 */
static int
do_mount_syscall (struct mountargs *args) {
     int ret = mount (args->spec, args->node, args->type,
		      MS_MGC_VAL | (args->flags), args->data);
     if (ret == 0)
	  mountcount++;
     return ret;
}

/*
 * guess_fstype_and_mount()
 *	Mount a single file system. Guess the type when unknown.
 * returns: 0: OK, -1: error in errno, 1: other error
 *	don't exit on non-fatal errors.
 */
static int
guess_fstype_and_mount (char *spec, char *node, char **type,
			int flags, char *mount_opts) {
   struct mountargs args = { spec, node, NULL, flags & ~MS_NOSYS, mount_opts };
   
   if (*type && strcasecmp (*type, "auto") == 0)
      *type = NULL;

   if (!*type && !(flags & MS_REMOUNT)) {
      *type = guess_fstype_from_superblock(spec);
      if (*type && !strcmp(*type, "swap")) {
	  error(_("%s looks like swapspace - not mounted"), spec);
	  *type = NULL;
	  return 1;
      }
   }

   if (*type || (flags & MS_REMOUNT)) {
      args.type = *type;
      return do_mount_syscall (&args);
   }

   return procfsloop(do_mount_syscall, &args, type);
}

/*
 * suid_check()
 *	Die if the user is not allowed to do this.
 */
static void
suid_check(char *spec, char *node, int *flags, char **user) {
  if (suid) {
      /* RedHat patch: allow owners to mount when fstab contains
	 the owner option.  Note that this should never be used
         in a high security environment, but may be useful to give
         people at the console the possibility of mounting a floppy. */
      if (*flags & MS_OWNER) {
	  if (!strncmp(spec, "/dev/", 5)) {
	      struct stat sb;

	      if (!stat(spec, &sb)) {
		  if (getuid() == sb.st_uid)
		      *flags |= MS_USER;
	      }
	  }
      }
      /* James Kehl <mkehl@gil.com.au> came with a similar patch:
	 allow an arbitrary user to mount when he is the owner of
	 the mount-point and has write-access to the device.
         This is even less secure. Let me skip it for the time being;
         there should be an explicit fstab line allowing such things. */

      if (!(*flags & (MS_USER | MS_USERS))) {
          if (already (spec, node))
	    die (EX_USAGE, _("mount failed"));
	  else
            die (EX_USAGE, _("mount: only root can mount %s on %s"), spec, node);
      }
      if (*flags & MS_USER)
	  *user = getusername();
  }

  if (*flags & MS_OWNER)
      *flags &= ~MS_OWNER;
}

static int
loop_check(char **spec, char **type, int *flags,
	   int *loop, char **loopdev, char **loopfile) {
  int looptype, offset;

  /*
   * In the case of a loop mount, either type is of the form lo@/dev/loop5
   * or the option "-o loop=/dev/loop5" or just "-o loop" is given, or
   * mount just has to figure things out for itself from the fact that
   * spec is not a block device. We do not test for a block device
   * immediately: maybe later other types of mountable objects will occur.
   */

  *loopdev = opt_loopdev;

  looptype = (*type && strncmp("lo@", *type, 3) == 0);
  if (looptype) {
    if (*loopdev)
      error(_("mount: loop device specified twice"));
    *loopdev = *type + 3;
    *type = opt_vfstype;
  } else if (opt_vfstype) {
    if (*type)
      error(_("mount: type specified twice"));
    else
      *type = opt_vfstype;
  }

  *loop = ((*flags & MS_LOOP) || *loopdev || opt_offset || opt_encryption);
  *loopfile = *spec;

  if (*loop) {
    *flags |= MS_LOOP;
    if (fake) {
      if (verbose)
	printf(_("mount: skipping the setup of a loop device\n"));
    } else {
      int loopro = (*flags & MS_RDONLY);

      if (!*loopdev || !**loopdev)
	*loopdev = find_unused_loop_device();
      if (!*loopdev)
	return EX_SYSERR;	/* no more loop devices */
      if (verbose)
	printf(_("mount: going to use the loop device %s\n"), *loopdev);
      offset = opt_offset ? strtoul(opt_offset, NULL, 0) : 0;
      if (set_loop (*loopdev, *loopfile, offset, opt_encryption, &loopro)) {
	if (verbose)
	  printf(_("mount: failed setting up loop device\n"));
	return EX_FAIL;
      }
      if (verbose > 1)
	printf(_("mount: setup loop device successfully\n"));
      *spec = *loopdev;
      if (loopro)
	*flags |= MS_RDONLY;
    }
  }

  return 0;
}

static void
update_mtab_entry(char *spec, char *node, char *type, char *opts,
		  int flags, int freq, int pass) {
    struct mntentchn mcn;
    struct mntent mnt;

    mcn.mnt_fsname = mnt.mnt_fsname = canonicalize (spec);
    mcn.mnt_dir = mnt.mnt_dir = canonicalize (node);
    mcn.mnt_type = mnt.mnt_type = type;
    mcn.mnt_opts = mnt.mnt_opts = opts;
    mcn.nxt = 0;
    mnt.mnt_freq = freq;
    mnt.mnt_passno = pass;
      
    /* We get chatty now rather than after the update to mtab since the
       mount succeeded, even if the write to /etc/mtab should fail.  */
    if (verbose)
	print_one (&mcn);

    if (!nomtab && mtab_is_writable()) {
	if (flags & MS_REMOUNT)
	    update_mtab (mnt.mnt_dir, &mnt);
	else {
	    mntFILE *mfp;

	    lock_mtab();
	    mfp = my_setmntent(MOUNTED, "a+");
	    if (mfp == NULL || mfp->mntent_fp == NULL) {
	       	int errsv = errno;
		error(_("mount: can't open %s: %s"), MOUNTED,
		      strerror (errsv));
	    } else {
		if ((my_addmntent (mfp, &mnt)) == 1) {
			int errsv = errno;
			error(_("mount: error writing %s: %s"), MOUNTED,
			      strerror (errsv));
		}
		my_endmntent(mfp);
	    }
	    unlock_mtab();
	}
    }
}

static void
cdrom_setspeed(char *spec) {
#define CDROM_SELECT_SPEED      0x5322  /* Set the CD-ROM speed */
    if (opt_speed) {
	int cdrom;
	int speed = atoi(opt_speed);

	if ((cdrom = open(spec, O_RDONLY | O_NONBLOCK)) < 0)
	    die(EX_FAIL, _("mount: cannot not open %s for setting speed"),
		spec);
	if (ioctl(cdrom, CDROM_SELECT_SPEED, speed) < 0)
	    die(EX_FAIL, _("mount: cannot set speed: %s"),
		strerror(errno));
	close(cdrom);
    }
}

/*
 * check_special_mountprog()
 *	If there is a special mount program for this type, exec it.
 * returns: 0: no exec was done, 1: exec was done, status has result
 */
static int
check_special_mountprog(char *spec, char *node, char *type, int flags,
			char *extra_opts, int *status) {
  char mountprog[120];
  struct stat statbuf;
  int res;

  if (type &&
#ifndef ALWAYS_STAT
     (streq (type, "smb") || streq (type, "ncp")
      /* these are incorrect but perhaps used by smbmount or so */
       || streq (type, "smbfs") || streq (type, "ncpfs")
     )
#else
     (strlen (type) < 100)
#endif
  ) {
       sprintf(mountprog, "/sbin/mount.%s", type);
       if (stat(mountprog, &statbuf) == 0) {
	    res = fork();
	    if (res == 0) {
		 char *oo, *mountargs[10];
		 int i = 0;

		 setuid(getuid());
		 setgid(getgid());
		 oo = fix_opts_string (flags, extra_opts, NULL);
		 mountargs[i++] = mountprog;
		 mountargs[i++] = spec;
		 mountargs[i++] = node;
		 if (nomtab)
		      mountargs[i++] = "-n";
		 if (verbose)
		      mountargs[i++] = "-v";
		 if (oo && *oo) {
		      mountargs[i++] = "-o";
		      mountargs[i++] = oo;
		 }
		 mountargs[i] = NULL;
		 execv(mountprog, mountargs);
		 exit(1);	/* exec failed */
	    } else if (res != -1) {
		 int st;
		 wait(&st);
		 *status = (WIFEXITED(st) ? WEXITSTATUS(st) : EX_SYSERR);
		 return 1;
	    } else {
	    	 int errsv = errno;
		 error(_("mount: cannot fork: %s"), strerror(errsv));
	    }
       }
  }
  return 0;
}

/*
 * try_mount_one()
 *	Try to mount one file system. When "bg" is 1, this is a retry
 *	in the background. One additional exit code EX_BG is used here.
 *	It is used to instruct the caller to retry the mount in the
 *	background.
 * returns: 0: OK, EX_SYSERR, EX_FAIL, return code from nfsmount,
 *      return status from wait
 */
static int
try_mount_one (const char *spec0, const char *node0, char *type0,
	       const char *opts0, int freq, int pass, int bg, int ro) {
  int res, status;
  int mnt5_res = 0;		/* only for gcc */
  int mnt_err;
  int flags;
  char *extra_opts;		/* written in mtab */
  char *mount_opts;		/* actually used on system call */
  const char *opts;
  char *spec, *node, *type;
  char *user = 0;
  int loop = 0;
  char *loopdev = 0, *loopfile = 0;
  struct stat statbuf;

  spec = xstrdup(spec0);
  node = xstrdup(node0);
  type = xstrdup(type0);
  opts = xstrdup(opts0);

  parse_opts (xstrdup (opts), &flags, &extra_opts);

  suid_check (spec, node, &flags, &user);

  /* quietly succeed for fstab entries that don't get mounted automatically */
  if (all && (flags & MS_NOAUTO))
    return 0;

  mount_opts = extra_opts;

  if (opt_speed)
      cdrom_setspeed(spec);

  res = loop_check (&spec, &type, &flags, &loop, &loopdev, &loopfile);
  if (res)
	  return res;

  /*
   * Call mount.TYPE for types that require a separate mount program.
   * For the moment these types are ncp and smb.
   */
  if (check_special_mountprog (spec, node, type, flags, extra_opts, &status))
      return status;

  if (!fake && type && streq (type, "nfs")) {
#ifdef HAVE_NFS
retry_nfs:
    mnt_err = nfsmount (spec, node, &flags, &extra_opts, &mount_opts, bg);
    if (mnt_err)
      return mnt_err;
#else
    die (EX_SOFTWARE, _("mount: this version was compiled "
	              "without support for the type `nfs'"));
#endif
  }

  block_signals (SIG_BLOCK);

  if (!fake)
    mnt5_res = guess_fstype_and_mount (spec, node, &type, flags & ~MS_NOSYS,
				       mount_opts);

  if (fake || mnt5_res == 0) {
      /* Mount succeeded, report this (if verbose) and write mtab entry.  */
      if (loop)
	  opt_loopdev = loopdev;

      update_mtab_entry(loop ? loopfile : spec,
			node,
			type ? type : "unknown",
			fix_opts_string (flags & ~MS_NOMTAB, extra_opts, user),
			flags,
			freq,
			pass);

      block_signals (SIG_UNBLOCK);
      return 0;
  }

  mnt_err = errno;

  if (loop)
	del_loop(spec);

  block_signals (SIG_UNBLOCK);

#ifdef HAVE_NFS
  if (mnt_err && type && streq (type, "nfs")) {
      extern int nfs_mount_version;
      if (nfs_mount_version == 4) {
	  if (verbose)
	    printf(_("mount: failed with nfs mount version 4, trying 3..\n"));
	  nfs_mount_version = 3;
	  goto retry_nfs;
      }
  }
#endif

  /* Mount failed, complain, but don't die.  */

  if (type == 0) {
    if (suid)
      error (_("mount: I could not determine the filesystem type, "
	       "and none was specified"));
    else
      error (_("mount: you must specify the filesystem type"));
  } else if (mnt5_res != -1) {
      /* should not happen */
      error (_("mount: mount failed"));
  } else {
   switch (mnt_err) {
    case EPERM:
      if (geteuid() == 0) {
	   if (stat (node, &statbuf) || !S_ISDIR(statbuf.st_mode))
		error (_("mount: mount point %s is not a directory"), node);
	   else
		error (_("mount: permission denied"));
      } else
	error (_("mount: must be superuser to use mount"));
      break;
    case EBUSY:
      if (flags & MS_REMOUNT) {
	error (_("mount: %s is busy"), node);
      } else if (!strcmp(type, "proc") && !strcmp(node, "/proc")) {
	/* heuristic: if /proc/version exists, then probably proc is mounted */
	if (stat ("/proc/version", &statbuf))   /* proc mounted? */
	   error (_("mount: %s is busy"), node);   /* no */
	else if(!all || verbose)                /* yes, don't mention it */
	   error (_("mount: proc already mounted"));
      } else {
	error (_("mount: %s already mounted or %s busy"), spec, node);
	already (spec, node);
      }
      break;
    case ENOENT:
      if (lstat (node, &statbuf))
	   error (_("mount: mount point %s does not exist"), node);
      else if (stat (node, &statbuf))
	   error (_("mount: mount point %s is a symbolic link to nowhere"),
		  node);
      else if (stat (spec, &statbuf))
	   error (_("mount: special device %s does not exist"), spec);
      else {
           errno = mnt_err;
           perror("mount");
      }
      break;
    case ENOTDIR:
      if (stat (node, &statbuf) || ! S_ISDIR(statbuf.st_mode))
           error (_("mount: mount point %s is not a directory"), node);
      else if (stat (spec, &statbuf) && errno == ENOTDIR)
	   error (_("mount: special device %s does not exist\n"
		    "       (a path prefix is not a directory)\n"), spec);
      else {
	   errno = mnt_err;
	   perror("mount");
      }
      break;
    case EINVAL:
    { int fd;
      long size;

      if (flags & MS_REMOUNT) {
	error (_("mount: %s not mounted already, or bad option"), node);
      } else {
	error (_("mount: wrong fs type, bad option, bad superblock on %s,\n"
	       "       or too many mounted file systems"),
	       spec);

	if (stat (spec, &statbuf) == 0 && S_ISBLK(statbuf.st_mode)
	   && (fd = open(spec, O_RDONLY | O_NONBLOCK)) >= 0) {
	  if(ioctl(fd, BLKGETSIZE, &size) == 0 && size <= 2)
	  error ("       (aren't you trying to mount an extended partition,\n"
		 "       instead of some logical partition inside?)");
	  close(fd);
	}
      }
      break;
    }
    case EMFILE:
      error (_("mount table full")); break;
    case EIO:
      error (_("mount: %s: can't read superblock"), spec); break;
    case ENODEV:
      if (is_in_procfs(type) || !strcmp(type, "guess"))
        error(_("mount: %s has wrong major or minor number"), spec);
      else if (have_procfs()) {
	char *lowtype, *p;
	int u;

	error (_("mount: fs type %s not supported by kernel"), type);

	/* maybe this loser asked for FAT or ISO9660 or isofs */
	lowtype = xstrdup(type);
	u = 0;
	for(p=lowtype; *p; p++) {
	  if(tolower(*p) != *p) {
	    *p = tolower(*p);
	    u++;
	  }
	}
	if (u && is_in_procfs(lowtype))
	  error (_("mount: probably you meant %s"), lowtype);
	else if (!strncmp(lowtype, "iso", 3) && is_in_procfs("iso9660"))
	  error (_("mount: maybe you meant iso9660 ?"));
	free(lowtype);
      } else
	error (_("mount: %s has wrong device number or fs type %s not supported"),
	       spec, type);
      break;
    case ENOTBLK:
      if (stat (spec, &statbuf)) /* strange ... */
	error (_("mount: %s is not a block device, and stat fails?"), spec);
      else if (S_ISBLK(statbuf.st_mode))
        error (_("mount: the kernel does not recognize %s as a block device\n"
	       "       (maybe `insmod driver'?)"), spec);
      else if (S_ISREG(statbuf.st_mode))
	error (_("mount: %s is not a block device (maybe try `-o loop'?)"),
		 spec);
      else
	error (_("mount: %s is not a block device"), spec);
      break;
    case ENXIO:
      error (_("mount: %s is not a valid block device"), spec); break;
    case EACCES:  /* pre-linux 1.1.38, 1.1.41 and later */
    case EROFS:   /* linux 1.1.38 and later */
    { char *bd = (loop ? "" : _("block device "));
      if (ro) {
          error (_("mount: %s%s is not permitted on its filesystem"),
		 bd, spec);
          break;
      } else if (readwrite) {
	  error (_("mount: %s%s is write-protected but explicit `-w' flag given"),
		 bd, spec);
	  break;
      } else {
	 if (loop) {
	     opts = opts0;
	     type = type0;
	 }
         if (opts) {
	     char *opts1 = realloc(xstrdup(opts), strlen(opts)+4);
             strcat(opts1, ",ro");
	     opts = opts1;
         } else
             opts = "ro";
	 if (type && !strcmp(type, "guess"))
	     type = 0;
         error (_("mount: %s%s is write-protected, mounting read-only"),
		bd, spec0);
	 return try_mount_one (spec0, node0, type, opts, freq, pass, bg, 1);
      }
      break;
    }
    default:
      error ("mount: %s", strerror (mnt_err)); break;
    }
  }
  return EX_FAIL;
}

/*
 * set_proc_name()
 *	Update the argument vector, so that this process may be easily
 *	identified in a "ps" listing.
 */
static void
set_proc_name (const char *spec)
{
#ifdef DO_PS_FIDDLING
	setproctitle ("mount", spec);
#endif
}

static char *
subst_string(const char *s, const char *sub, int sublen, const char *repl) {
	char *n;

	n = (char *) xmalloc(strlen(s)-sublen+strlen(repl)+1);
	strncpy (n, s, sub-s);
	strcpy (n + (sub-s), repl);
	strcat (n, sub+sublen);
	return n;
}

static const char *
usersubst(const char *opts) {
	char *s, *w;
	char id[40];

	s = "uid=useruid";
	if (opts && (w = strstr(opts, s)) != NULL) {
		sprintf(id, "uid=%d", getuid());
		opts = subst_string(opts, w, strlen(s), id);
	}
	s = "gid=usergid";
	if (opts && (w = strstr(opts, s)) != NULL) {
		sprintf(id, "gid=%d", getgid());
		opts = subst_string(opts, w, strlen(s), id);
	}
	return opts;
}
		

/*
 * Return 0 for success (either mounted sth or -a and NOAUTO was given)
 */
static int
mount_one (const char *spec, const char *node, char *type, const char *opts,
	   char *cmdlineopts, int freq, int pass) {
  int status;
  int status2;
  int specset = 0;
  char *nspec;

  /* Substitute values in opts, if required */
  opts = usersubst(opts);

  /* Merge the fstab and command line options.  */
  if (opts == NULL)
       opts = cmdlineopts;
  else if (cmdlineopts != NULL)
       opts = xstrconcat3(opts, ",", cmdlineopts);

  if (!strncmp(spec, "UUID=", 5)) {
      nspec = get_spec_by_uuid(spec+5);
      specset = 1;
  } else if (!strncmp(spec, "LABEL=", 6)) {
      nspec = get_spec_by_volume_label(spec+6);
      specset = 2;
  } else
      nspec = 0;		/* just for gcc */

  if (specset) {
      if (nspec) {
	  spec = nspec;
	  if (verbose)
		  printf(_("mount: consider mounting %s by %s\n"), spec,
			 (specset==1) ? _("UUID") : _("label"));
      } else if(!all)
          die (EX_USAGE, _("mount: no such partition found"));
      /* if -a then we may be rescued by a noauto option */
  }

  if (type == NULL) {
      if (strchr (spec, ':') != NULL) {
	type = "nfs";
	if (verbose)
	  printf(_("mount: no type was given - "
		 "I'll assume nfs because of the colon\n"));
      }
  }

  /*
   * Try to mount the file system. When the exit status is EX_BG,
   * we will retry in the background. Otherwise, we're done.
   */
  status = try_mount_one (spec, node, type, opts, freq, pass, 0, 0);
  if (status != EX_BG)
    return status;

  /*
   * Retry in the background.
   */
  printf (_("mount: backgrounding \"%s\"\n"), spec);
  fflush( stdout );		/* prevent duplicate output */
  if (fork() > 0)
    return 0;			/* parent returns "success" */
  spec = xstrdup(spec);		/* arguments will be destroyed */
  node = xstrdup(node);		/* by set_proc_name()          */
  type = xstrdup(type);
  opts = xstrdup(opts);
  set_proc_name (spec);		/* make a nice "ps" listing */
  status2 = try_mount_one (spec, node, type, opts, freq, pass, 1, 0);
  if (verbose && status2)
    printf (_("mount: giving up \"%s\"\n"), spec);
  exit (0);			/* child stops here */
}

/* Check if an fsname/dir pair was already in the old mtab.  */
static int
mounted (char *spec, char *node) {
     struct mntentchn *mc;
     char *nspec = NULL;

     if (!strncmp(spec, "UUID=", 5))
	  nspec = get_spec_by_uuid(spec+5);
     else if (!strncmp(spec, "LABEL=", 6))
	  nspec = get_spec_by_volume_label(spec+6);

     if (nspec)
	  spec = nspec;

     spec = canonicalize (spec);
     node = canonicalize (node);

     for (mc = mtab_head()->nxt; mc; mc = mc->nxt)
	  if (streq (spec, mc->mnt_fsname) && streq (node, mc->mnt_dir))
	       return 1;
     return 0;
}

/* Mount all filesystems of the specified types except swap and root.  */
/* With the --fork option: fork and let different incarnations of
   mount handle different filesystems.  However, try to avoid several
   simultaneous mounts on the same physical disk, since that is very slow. */
#define DISKMAJOR(m)	(((int) m) & ~0xf)

static int
mount_all (string_list types, char *options) {
     struct mntentchn *mc, *mtmp;
     int status = 0;
     struct stat statbuf;
     struct child {
	  pid_t pid;
	  char *group;
	  struct mntentchn *mec;
	  struct mntentchn *meclast;
	  struct child *nxt;
     } childhead, *childtail, *cp;
     char major[22];
     char *g, *colon;

     /* build a chain of what we have to do, or maybe
	several chains, one for each major or NFS host */
     childhead.nxt = 0;
     childtail = &childhead;
     for (mc = fstab_head()->nxt; mc; mc = mc->nxt) {
	  if (matching_type (mc->mnt_type, types)
	      && !streq (mc->mnt_dir, "/")
	      && !streq (mc->mnt_dir, "root")) {
	       if (mounted (mc->mnt_fsname, mc->mnt_dir)) {
		    if (verbose)
			 printf(_("mount: %s already mounted on %s\n"),
				mc->mnt_fsname, mc->mnt_dir);
	       } else {
		    mtmp = (struct mntentchn *) xmalloc(sizeof(*mtmp));
		    *mtmp = *mc;
		    mtmp->nxt = 0;
		    g = NULL;
		    if (optfork) {
			 if (stat(mc->mnt_fsname, &statbuf) == 0 &&
			     S_ISBLK(statbuf.st_mode)) {
			      sprintf(major, "#%x", DISKMAJOR(statbuf.st_rdev));
			      g = major;
			 }
#ifdef HAVE_NFS
			 if (strcmp(mc->mnt_type, "nfs") == 0) {
			      g = xstrdup(mc->mnt_fsname);
			      colon = strchr(g, ':');
			      if (colon)
				   *colon = '\0';
			 }
#endif
		    }
		    if (g) {
			 for (cp = childhead.nxt; cp; cp = cp->nxt)
			      if (cp->group && strcmp(cp->group, g) == 0) {
				   cp->meclast->nxt = mtmp;
				   cp->meclast = mtmp;
				   goto fnd;
			      }
		    }
		    cp = (struct child *) xmalloc(sizeof *cp);
		    cp->nxt = 0;
		    cp->mec = cp->meclast = mtmp;
		    cp->group = xstrdup(g);
		    cp->pid = 0;
		    childtail->nxt = cp;
		    childtail = cp;
	       fnd:;
	       }
	  }
     }
			      
     /* now do everything */
     for (cp = childhead.nxt; cp; cp = cp->nxt) {
	  pid_t p = -1;
	  if (optfork) {
	       p = fork();
	       if (p == -1) {
	       	    int errsv = errno;
		    error(_("mount: cannot fork: %s"), strerror (errsv));
	       }
	       else if (p != 0)
		    cp->pid = p;
	  }

	  /* if child, or not forked, do the mounting */
	  if (p == 0 || p == -1) {
	       for (mc = cp->mec; mc; mc = mc->nxt)
		    status |= mount_one (mc->mnt_fsname, mc->mnt_dir,
					 mc->mnt_type, mc->mnt_opts,
					 options, 0, 0);
	       if (mountcount)
		    status |= EX_SOMEOK;
	       if (p == 0)
		    exit(status);
	  }
     }

     /* wait for children, if any */
     while ((cp = childhead.nxt) != NULL) {
	  childhead.nxt = cp->nxt;
	  if (cp->pid) {
	       int ret;
	  keep_waiting:
	       if(waitpid(cp->pid, &ret, 0) == -1) {
		    if (errno == EINTR)
			 goto keep_waiting;
		    perror("waitpid");
	       } else if (WIFEXITED(ret))
		    status |= WEXITSTATUS(ret);
	       else
		    status |= EX_SYSERR;
	  }
     }
     if (mountcount)
	  status |= EX_SOMEOK;
     return status;
}

extern char version[];
static struct option longopts[] =
{
  { "all", 0, 0, 'a' },
  { "fake", 0, 0, 'f' },
  { "fork", 0, 0, 'F' },
  { "help", 0, 0, 'h' },
  { "no-mtab", 0, 0, 'n' },
  { "read-only", 0, 0, 'r' },
  { "ro", 0, 0, 'r' },
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { "read-write", 0, 0, 'w' },
  { "rw", 0, 0, 'w' },
  { "options", 1, 0, 'o' },
  { "types", 1, 0, 't' },
  { NULL, 0, 0, 0 }
};

static void
usage (FILE *fp, int n)
{
  fprintf (fp, _("Usage: mount [-lhV]\n"
	       "       mount -a [-nfFrsvw] [-t vfstypes]\n"
	       "       mount [-nfrsvw] [-o options] special | node\n"
	       "       mount [-nfrsvw] [-t vfstype] [-o options] special node\n"
	       "       A special device can be indicated by  -L label  or  -U uuid .\n"));
  unlock_mtab();
  exit (n);
}

int
main (int argc, char *argv[]) {
  int c, result = 0, specseen;
  char *options = NULL, *spec, *node;
  char *volumelabel = NULL;
  char *uuid = NULL;
  string_list types = NULL;
  struct mntentchn *mc;
  int fd;

#ifdef HAVE_SETLOCALE
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif

  /* People report that a mount called from init without console
     writes error messages to /etc/mtab
     Let us try to avoid getting fd's 0,1,2 */
  while((fd = open("/dev/null", O_RDWR)) == 0 || fd == 1 || fd == 2) ;
  if (fd > 2)
     close(fd);

#ifdef DO_PS_FIDDLING
  initproctitle(argc, argv);
#endif

  while ((c = getopt_long (argc, argv, "afFhlL:no:rsU:vVwt:", longopts, NULL))
	 != EOF)
    switch (c) {
      case 'a':			/* mount everything in fstab */
	++all;
	break;
      case 'f':			/* fake (don't actually do mount(2) call) */
	++fake;
	break;
      case 'F':
	++optfork;
	break;
      case 'h':			/* help */
	usage (stdout, 0);
	break;
      case 'l':
	list_with_volumelabel = 1;
	break;
      case 'L':
	volumelabel = optarg;
	break;
      case 'n':			/* mount without writing in /etc/mtab */
	++nomtab;
	break;
      case 'o':			/* specify mount options */
	if (options)
	     options = xstrconcat3(options, ",", optarg);
	else
	     options = xstrdup(optarg);
	break;
      case 'r':			/* mount readonly */
	readonly = 1;
	readwrite = 0;
	break;
      case 's':			/* allow sloppy mount options */
	sloppy = 1;
	break;
      case 't':			/* specify file system types */
	types = parse_list (optarg);
	break;
      case 'U':
	uuid = optarg;
	break;
      case 'v':			/* be chatty - very chatty if repeated */
	++verbose;
	break;
      case 'V':			/* version */
	printf ("mount: %s\n", version);
	exit (0);
      case 'w':			/* mount read/write */
	readwrite = 1;
	readonly = 0;
	break;
      case 0:
	break;
      case '?':
      default:
	usage (stderr, EX_USAGE);
    }

  argc -= optind;
  argv += optind;

  specseen = (uuid || volumelabel) ? 1 : 0; 	/* yes, .. i know */

  if (argc+specseen == 0 && !all) {
      if (options)
	usage (stderr, EX_USAGE);
      return print_all (types);
  }

  if (getuid () != geteuid ()) {
      suid = 1;
      if (types || options || readwrite || nomtab || all || fake ||
	  (argc + specseen) != 1)
	die (EX_USAGE, _("mount: only root can do that"));
  }

  if (!nomtab && mtab_does_not_exist()) {
       if (verbose > 1)
	    printf(_("mount: no %s found - creating it..\n"), MOUNTED);
       create_mtab ();
  }

  if (specseen) {
	  if (uuid)
		  spec = get_spec_by_uuid(uuid);
	  else
		  spec = get_spec_by_volume_label(volumelabel);
	  if (!spec)
		  die (EX_USAGE, _("mount: no such partition found"));
	  if (verbose)
		  printf(_("mount: mounting %s\n"), spec);
  } else
	  spec = NULL;		/* just for gcc */

  switch (argc+specseen) {
    case 0:
      /* mount -a */
      result = mount_all (types, options);
      if (result == 0 && verbose)
	   error(_("not mounted anything"));
      break;

    case 1:
      /* mount [-nfrvw] [-o options] special | node */
      if (types != NULL)
	usage (stderr, EX_USAGE);
      if (specseen) {
	      /* We know the device. Where shall we mount it? */
	      mc = (uuid ? getfsuuidspec (uuid) : getfsvolspec (volumelabel));
	      if (mc == NULL)
		      mc = getfsspec (spec);
	      if (mc == NULL)
		      die (EX_USAGE, _("mount: cannot find %s in %s"),
			   spec, _PATH_FSTAB);
	      mc->mnt_fsname = spec;
      } else {
	      /* Try to find the other pathname in fstab.  */
	      spec = canonicalize (*argv);
	      if ((mc = getfsspec (spec)) == NULL &&
		  (mc = getfsfile (spec)) == NULL &&
	      /* Try noncanonical name in fstab
	         perhaps /dev/cdrom or /dos is a symlink */
		  (mc = getfsspec (*argv)) == NULL &&
		  (mc = getfsfile (*argv)) == NULL &&
	      /* Try mtab - maybe this was a remount */
		  (mc = getmntfile (spec)) == NULL)
		      die (EX_USAGE, _("mount: can't find %s in %s or %s"),
			   spec, _PATH_FSTAB, MOUNTED);
	      /* Earlier mtab was tried first, but this would
		 sometimes try the wrong mount in case mtab had
		 the root device entry wrong. */
      }

      result = mount_one (xstrdup (mc->mnt_fsname), xstrdup (mc->mnt_dir),
			  xstrdup (mc->mnt_type), mc->mnt_opts, options, 0, 0);
      break;

    case 2:
      /* mount [-nfrvw] [-t vfstype] [-o options] special node */
      if (specseen) {
	      /* we have spec already */
	      node = argv[0];
      } else {
	      spec = argv[0];
	      node = argv[1];
      }
      if (types == NULL)
	result = mount_one (spec, node, NULL, NULL, options, 0, 0);
      else if (cdr (types) == NULL)
	result = mount_one (spec, node, car (types), NULL, options, 0, 0);
      else
	usage (stderr, EX_USAGE);
      break;
      
    default:
      usage (stderr, EX_USAGE);
  }

  if (result == EX_SOMEOK)
       result = 0;
  exit (result);
}
