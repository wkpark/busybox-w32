/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) tons of folks.  Tracking down who wrote what
 * isn't something I'm going to worry about...  If you wrote something
 * here, please feel free to acknowledge your work.
 *
 * Based in part on code from sash, Copyright (c) 1999 by David I. Bell
 * Permission has been granted to redistribute this code under GPL.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* We are trying to not use printf, this benefits the case when selected
 * applets are really simple. Example:
 *
 * $ ./busybox
 * ...
 * Currently defined functions:
 *         basename, false, true
 *
 * $ size busybox
 *    text    data     bss     dec     hex filename
 *    4473      52      72    4597    11f5 busybox
 *
 * FEATURE_INSTALLER or FEATURE_SUID will still link printf routines in. :(
 */
#include "busybox.h"

#if !(defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
        || defined(__APPLE__) \
    )
# include <malloc.h> /* for mallopt */
#endif


/* Declare <applet>_main() */
#define PROTOTYPES
#include "applets.h"
#undef PROTOTYPES

/* Include generated applet names, pointers to <applet>_main, etc */
#include "applet_tables.h"
/* ...and if applet_tables generator says we have only one applet... */
#ifdef SINGLE_APPLET_MAIN
# undef ENABLE_FEATURE_INDIVIDUAL
# define ENABLE_FEATURE_INDIVIDUAL 1
# undef IF_FEATURE_INDIVIDUAL
# define IF_FEATURE_INDIVIDUAL(...) __VA_ARGS__
#endif

#include "usage_compressed.h"


#if ENABLE_SHOW_USAGE && !ENABLE_FEATURE_COMPRESS_USAGE
static const char usage_messages[] ALIGN1 = UNPACKED_USAGE;
#else
# define usage_messages 0
#endif

#if ENABLE_FEATURE_COMPRESS_USAGE

static const char packed_usage[] ALIGN1 = { PACKED_USAGE };
# include "archive.h"
static const char *unpack_usage_messages(void)
{
	char *outbuf = NULL;
	bunzip_data *bd;
	int i;

	i = start_bunzip(&bd,
			/* src_fd: */ -1,
			/* inbuf:  */ packed_usage,
			/* len:    */ sizeof(packed_usage));
	/* read_bunzip can longjmp to start_bunzip, and ultimately
	 * end up here with i != 0 on read data errors! Not trivial */
	if (!i) {
		/* Cannot use xmalloc: will leak bd in NOFORK case! */
		outbuf = malloc_or_warn(sizeof(UNPACKED_USAGE));
		if (outbuf)
			read_bunzip(bd, outbuf, sizeof(UNPACKED_USAGE));
	}
	dealloc_bunzip(bd);
	return outbuf;
}
# define dealloc_usage_messages(s) free(s)

#else

# define unpack_usage_messages() usage_messages
# define dealloc_usage_messages(s) ((void)(s))

#endif /* FEATURE_COMPRESS_USAGE */


void FAST_FUNC bb_show_usage(void)
{
	if (ENABLE_SHOW_USAGE) {
#ifdef SINGLE_APPLET_STR
		/* Imagine that this applet is "true". Dont suck in printf! */
		const char *usage_string = unpack_usage_messages();

		if (*usage_string == '\b') {
			full_write2_str("No help available.\n\n");
		} else {
			full_write2_str("Usage: "SINGLE_APPLET_STR" ");
			full_write2_str(usage_string);
			full_write2_str("\n\n");
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			dealloc_usage_messages((char*)usage_string);
#else
		const char *p;
		const char *usage_string = p = unpack_usage_messages();
		int ap = find_applet_by_name(applet_name);

		if (ap < 0) /* never happens, paranoia */
			xfunc_die();
		while (ap) {
			while (*p++) continue;
			ap--;
		}
		full_write2_str(bb_banner);
		full_write2_str(" multi-call binary.\n");
		if (*p == '\b')
			full_write2_str("\nNo help available.\n\n");
		else {
			full_write2_str("\nUsage: ");
			full_write2_str(applet_name);
			full_write2_str(" ");
			full_write2_str(p);
			full_write2_str("\n\n");
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			dealloc_usage_messages((char*)usage_string);
#endif
	}
	xfunc_die();
}

#if NUM_APPLETS > 8
/* NB: any char pointer will work as well, not necessarily applet_names */
static int applet_name_compare(const void *name, const void *v)
{
	int i = (const char *)v - applet_names;
	return strcmp(name, APPLET_NAME(i));
}
#endif
int FAST_FUNC find_applet_by_name(const char *name)
{
#if NUM_APPLETS > 8
	/* Do a binary search to find the applet entry given the name. */
	const char *p;
	p = bsearch(name, applet_names, ARRAY_SIZE(applet_main), 1, applet_name_compare);
	if (!p)
		return -1;
	return p - applet_names;
#else
	/* A version which does not pull in bsearch */
	int i = 0;
	const char *p = applet_names;
	while (i < NUM_APPLETS) {
		if (strcmp(name, p) == 0)
			return i;
		p += strlen(p) + 1;
		i++;
	}
	return -1;
#endif
}


void lbb_prepare(const char *applet
		IF_FEATURE_INDIVIDUAL(, char **argv))
				MAIN_EXTERNALLY_VISIBLE;
void lbb_prepare(const char *applet
		IF_FEATURE_INDIVIDUAL(, char **argv))
{
#ifdef __GLIBC__
	(*(int **)&bb_errno) = __errno_location();
	barrier();
#endif
	applet_name = applet;

	/* Set locale for everybody except 'init' */
	if (ENABLE_LOCALE_SUPPORT && getpid() != 1)
		setlocale(LC_ALL, "");

	IF_WIN32_NET(init_winsock();)

#if ENABLE_FEATURE_INDIVIDUAL
	/* Redundant for busybox (run_applet_and_exit covers that case)
	 * but needed for "individual applet" mode */
	if (argv[1]
	 && !argv[2]
	 && strcmp(argv[1], "--help") == 0
	 && strncmp(applet, "busybox", 7) != 0
	) {
		/* Special case. POSIX says "test --help"
		 * should be no different from e.g. "test --foo".  */
		if (!ENABLE_TEST || strcmp(applet_name, "test") != 0)
			bb_show_usage();
	}
#endif
}

/* The code below can well be in applets/applets.c, as it is used only
 * for busybox binary, not "individual" binaries.
 * However, keeping it here and linking it into libbusybox.so
 * (together with remaining tiny applets/applets.o)
 * makes it possible to avoid --whole-archive at link time.
 * This makes (shared busybox) + libbusybox smaller.
 * (--gc-sections would be even better....)
 */

const char *applet_name;
#if !BB_MMU
bool re_execed;
#endif


/* If not built as a single-applet executable... */
#if !defined(SINGLE_APPLET_MAIN)

IF_FEATURE_SUID(static uid_t ruid;)  /* real uid */

# if ENABLE_FEATURE_SUID_CONFIG

static struct suid_config_t {
	/* next ptr must be first: this struct needs to be llist-compatible */
	struct suid_config_t *m_next;
	struct bb_uidgid_t m_ugid;
	int m_applet;
	mode_t m_mode;
} *suid_config;

static bool suid_cfg_readable;

/* check if u is member of group g */
static int ingroup(uid_t u, gid_t g)
{
	struct group *grp = getgrgid(g);
	if (grp) {
		char **mem;
		for (mem = grp->gr_mem; *mem; mem++) {
			struct passwd *pwd = getpwnam(*mem);
			if (pwd && (pwd->pw_uid == u))
				return 1;
		}
	}
	return 0;
}

/* libbb candidate */
static char *get_trimmed_slice(char *s, char *e)
{
	/* First, consider the value at e to be nul and back up until we
	 * reach a non-space char.  Set the char after that (possibly at
	 * the original e) to nul. */
	while (e-- > s) {
		if (!isspace(*e)) {
			break;
		}
	}
	e[1] = '\0';

	/* Next, advance past all leading space and return a ptr to the
	 * first non-space char; possibly the terminating nul. */
	return skip_whitespace(s);
}

static void parse_config_file(void)
{
	/* Don't depend on the tools to combine strings. */
	static const char config_file[] ALIGN1 = "/etc/busybox.conf";

	struct suid_config_t *sct_head;
	int applet_no;
	FILE *f;
	const char *errmsg;
	unsigned lc;
	smallint section;
	struct stat st;

	ruid = getuid();
	if (ruid == 0) /* run by root - don't need to even read config file */
		return;

	if ((stat(config_file, &st) != 0)       /* No config file? */
	 || !S_ISREG(st.st_mode)                /* Not a regular file? */
	 || (st.st_uid != 0)                    /* Not owned by root? */
	 || (st.st_mode & (S_IWGRP | S_IWOTH))  /* Writable by non-root? */
	 || !(f = fopen_for_read(config_file))  /* Cannot open? */
	) {
		return;
	}

	suid_cfg_readable = 1;
	sct_head = NULL;
	section = lc = 0;

	while (1) {
		char buffer[256];
		char *s;

		if (!fgets(buffer, sizeof(buffer), f)) { /* Are we done? */
			// Looks like bloat
			//if (ferror(f)) {   /* Make sure it wasn't a read error. */
			//	errmsg = "reading";
			//	goto pe_label;
			//}
			fclose(f);
			suid_config = sct_head;	/* Success, so set the pointer. */
			return;
		}

		s = buffer;
		lc++;					/* Got a (partial) line. */

		/* If a line is too long for our buffer, we consider it an error.
		 * The following test does mistreat one corner case though.
		 * If the final line of the file does not end with a newline and
		 * yet exactly fills the buffer, it will be treated as too long
		 * even though there isn't really a problem.  But it isn't really
		 * worth adding code to deal with such an unlikely situation, and
		 * we do err on the side of caution.  Besides, the line would be
		 * too long if it did end with a newline. */
		if (!strchr(s, '\n') && !feof(f)) {
			errmsg = "line too long";
			goto pe_label;
		}

		/* Trim leading and trailing whitespace, ignoring comments, and
		 * check if the resulting string is empty. */
		s = get_trimmed_slice(s, strchrnul(s, '#'));
		if (!*s) {
			continue;
		}

		/* Check for a section header. */

		if (*s == '[') {
			/* Unlike the old code, we ignore leading and trailing
			 * whitespace for the section name.  We also require that
			 * there are no stray characters after the closing bracket. */
			char *e = strchr(s, ']');
			if (!e   /* Missing right bracket? */
			 || e[1] /* Trailing characters? */
			 || !*(s = get_trimmed_slice(s+1, e)) /* Missing name? */
			) {
				errmsg = "section header";
				goto pe_label;
			}
			/* Right now we only have one section so just check it.
			 * If more sections are added in the future, please don't
			 * resort to cascading ifs with multiple strcasecmp calls.
			 * That kind of bloated code is all too common.  A loop
			 * and a string table would be a better choice unless the
			 * number of sections is very small. */
			if (strcasecmp(s, "SUID") == 0) {
				section = 1;
				continue;
			}
			section = -1;	/* Unknown section so set to skip. */
			continue;
		}

		/* Process sections. */

		if (section == 1) {		/* SUID */
			/* Since we trimmed leading and trailing space above, we're
			 * now looking for strings of the form
			 *    <key>[::space::]*=[::space::]*<value>
			 * where both key and value could contain inner whitespace. */

			/* First get the key (an applet name in our case). */
			char *e = strchr(s, '=');
			if (e) {
				s = get_trimmed_slice(s, e);
			}
			if (!e || !*s) {	/* Missing '=' or empty key. */
				errmsg = "keyword";
				goto pe_label;
			}

			/* Ok, we have an applet name.  Process the rhs if this
			 * applet is currently built in and ignore it otherwise.
			 * Note: this can hide config file bugs which only pop
			 * up when the busybox configuration is changed. */
			applet_no = find_applet_by_name(s);
			if (applet_no >= 0) {
				unsigned i;
				struct suid_config_t *sct;

				/* Note: We currently don't check for duplicates!
				 * The last config line for each applet will be the
				 * one used since we insert at the head of the list.
				 * I suppose this could be considered a feature. */
				sct = xzalloc(sizeof(*sct));
				sct->m_applet = applet_no;
				/*sct->m_mode = 0;*/
				sct->m_next = sct_head;
				sct_head = sct;

				/* Get the specified mode. */

				e = skip_whitespace(e+1);

				for (i = 0; i < 3; i++) {
					/* There are 4 chars for each of user/group/other.
					 * "x-xx" instead of "x-" are to make
					 * "idx > 3" check catch invalid chars.
					 */
					static const char mode_chars[] ALIGN1 = "Ssx-" "Ssx-" "x-xx";
					static const unsigned short mode_mask[] ALIGN2 = {
						S_ISUID, S_ISUID|S_IXUSR, S_IXUSR, 0, /* Ssx- */
						S_ISGID, S_ISGID|S_IXGRP, S_IXGRP, 0, /* Ssx- */
						                          S_IXOTH, 0  /*   x- */
					};
					const char *q = strchrnul(mode_chars + 4*i, *e);
					unsigned idx = q - (mode_chars + 4*i);
					if (idx > 3) {
						errmsg = "mode";
						goto pe_label;
					}
					sct->m_mode |= mode_mask[q - mode_chars];
					e++;
				}

				/* Now get the user/group info. */

				s = skip_whitespace(e);
				/* Default is 0.0, else parse USER.GROUP: */
				if (*s) {
					/* We require whitespace between mode and USER.GROUP */
					if ((s == e) || !(e = strchr(s, '.'))) {
						errmsg = "uid.gid";
						goto pe_label;
					}
					*e = ':'; /* get_uidgid needs USER:GROUP syntax */
					if (get_uidgid(&sct->m_ugid, s, /*allow_numeric:*/ 1) == 0) {
						errmsg = "unknown user/group";
						goto pe_label;
					}
				}
			}
			continue;
		}

		/* Unknown sections are ignored. */

		/* Encountering configuration lines prior to seeing a
		 * section header is treated as an error.  This is how
		 * the old code worked, but it may not be desirable.
		 * We may want to simply ignore such lines in case they
		 * are used in some future version of busybox. */
		if (!section) {
			errmsg = "keyword outside section";
			goto pe_label;
		}

	} /* while (1) */

 pe_label:
	fclose(f);
	bb_error_msg("parse error in %s, line %u: %s", config_file, lc, errmsg);

	/* Release any allocated memory before returning. */
	llist_free((llist_t*)sct_head, NULL);
}
# else
static inline void parse_config_file(void)
{
	IF_FEATURE_SUID(ruid = getuid();)
}
# endif /* FEATURE_SUID_CONFIG */


# if ENABLE_FEATURE_SUID
static void check_suid(int applet_no)
{
	gid_t rgid;  /* real gid */

	if (ruid == 0) /* set by parse_config_file() */
		return; /* run by root - no need to check more */
	rgid = getgid();

#  if ENABLE_FEATURE_SUID_CONFIG
	if (suid_cfg_readable) {
		uid_t uid;
		struct suid_config_t *sct;
		mode_t m;

		for (sct = suid_config; sct; sct = sct->m_next) {
			if (sct->m_applet == applet_no)
				goto found;
		}
		goto check_need_suid;
 found:
		/* Is this user allowed to run this applet? */
		m = sct->m_mode;
		if (sct->m_ugid.uid == ruid)
			/* same uid */
			m >>= 6;
		else if ((sct->m_ugid.gid == rgid) || ingroup(ruid, sct->m_ugid.gid))
			/* same group / in group */
			m >>= 3;
		if (!(m & S_IXOTH)) /* is x bit not set? */
			bb_error_msg_and_die("you have no permission to run this applet");

		/* We set effective AND saved ids. If saved-id is not set
		 * like we do below, seteuid(0) can still later succeed! */

		/* Are we directed to change gid
		 * (APPLET = *s* USER.GROUP or APPLET = *S* USER.GROUP)?
		 */
		if (sct->m_mode & S_ISGID)
			rgid = sct->m_ugid.gid;
		/* else: we will set egid = rgid, thus dropping sgid effect */
		if (setresgid(-1, rgid, rgid))
			bb_perror_msg_and_die("setresgid");

		/* Are we directed to change uid
		 * (APPLET = s** USER.GROUP or APPLET = S** USER.GROUP)?
		 */
		uid = ruid;
		if (sct->m_mode & S_ISUID)
			uid = sct->m_ugid.uid;
		/* else: we will set euid = ruid, thus dropping suid effect */
		if (setresuid(-1, uid, uid))
			bb_perror_msg_and_die("setresuid");

		goto ret;
	}
#   if !ENABLE_FEATURE_SUID_CONFIG_QUIET
	{
		static bool onetime = 0;

		if (!onetime) {
			onetime = 1;
			bb_error_msg("using fallback suid method");
		}
	}
#   endif
 check_need_suid:
#  endif
	if (APPLET_SUID(applet_no) == BB_SUID_REQUIRE) {
		/* Real uid is not 0. If euid isn't 0 too, suid bit
		 * is most probably not set on our executable */
		if (geteuid())
			bb_error_msg_and_die("must be suid to work properly");
	} else if (APPLET_SUID(applet_no) == BB_SUID_DROP) {
		xsetgid(rgid);  /* drop all privileges */
		xsetuid(ruid);
	}
#  if ENABLE_FEATURE_SUID_CONFIG
 ret: ;
	llist_free((llist_t*)suid_config, NULL);
#  endif
}
# else
#  define check_suid(x) ((void)0)
# endif /* FEATURE_SUID */


# if ENABLE_FEATURE_INSTALLER
static const char usr_bin [] ALIGN1 = "/usr/bin/";
static const char usr_sbin[] ALIGN1 = "/usr/sbin/";
static const char *const install_dir[] = {
	&usr_bin [8], /* "/" */
	&usr_bin [4], /* "/bin/" */
	&usr_sbin[4]  /* "/sbin/" */
#  if !ENABLE_INSTALL_NO_USR
	,usr_bin
	,usr_sbin
#  endif
};

/* create (sym)links for each applet */
static void install_links(const char *busybox, int use_symbolic_links,
		char *custom_install_dir)
{
	/* directory table
	 * this should be consistent w/ the enum,
	 * busybox.h::bb_install_loc_t, or else... */
	int (*lf)(const char *, const char *);
	char *fpc;
	unsigned i;
	int rc;

	lf = link;
	if (use_symbolic_links)
		lf = symlink;

	for (i = 0; i < ARRAY_SIZE(applet_main); i++) {
		fpc = concat_path_file(
				custom_install_dir ? custom_install_dir : install_dir[APPLET_INSTALL_LOC(i)],
				APPLET_NAME(i));
		// debug: bb_error_msg("%slinking %s to busybox",
		//		use_symbolic_links ? "sym" : "", fpc);
		rc = lf(busybox, fpc);
		if (rc != 0 && errno != EEXIST) {
			bb_simple_perror_msg(fpc);
		}
		free(fpc);
	}
}
# else
#  define install_links(x,y,z) ((void)0)
# endif

/* If we were called as "busybox..." */
static int busybox_main(char **argv)
{
	if (!argv[1]) {
		/* Called without arguments */
		const char *a;
		int col;
		unsigned output_width;
 help:
		output_width = 80;
		if (ENABLE_FEATURE_AUTOWIDTH) {
			/* Obtain the terminal width */
			get_terminal_width_height(0, &output_width, NULL);
		}

		dup2(1, 2);
		full_write2_str(bb_banner); /* reuse const string */
		full_write2_str(" multi-call binary.\n"); /* reuse */
		full_write2_str(
			"Copyright (C) 1998-2011 Erik Andersen, Rob Landley, Denys Vlasenko\n"
			"and others. Licensed under GPLv2.\n"
			"See source distribution for full notice.\n"
			"\n"
			"Usage: busybox [function] [arguments]...\n"
			"   or: busybox --list[-full]\n"
			"   or: function [arguments]...\n"
			"\n"
			"\tBusyBox is a multi-call binary that combines many common Unix\n"
			"\tutilities into a single executable.  Most people will create a\n"
			"\tlink to busybox for each function they wish to use and BusyBox\n"
			"\twill act like whatever it was invoked as.\n"
			"\n"
			"Currently defined functions:\n"
		);
		col = 0;
		a = applet_names;
		/* prevent last comma to be in the very last pos */
		output_width--;
		while (*a) {
			int len2 = strlen(a) + 2;
			if (col >= (int)output_width - len2) {
				full_write2_str(",\n");
				col = 0;
			}
			if (col == 0) {
				col = 6;
				full_write2_str("\t");
			} else {
				full_write2_str(", ");
			}
			full_write2_str(a);
			col += len2;
			a += len2 - 1;
		}
		full_write2_str("\n\n");
		return 0;
	}

	if (strncmp(argv[1], "--list", 6) == 0) {
		unsigned i = 0;
		const char *a = applet_names;
		dup2(1, 2);
		while (*a) {
# if ENABLE_FEATURE_INSTALLER
			if (argv[1][6]) /* --list-path? */
				full_write2_str(install_dir[APPLET_INSTALL_LOC(i)] + 1);
# endif
			full_write2_str(a);
			full_write2_str("\n");
			i++;
			a += strlen(a) + 1;
		}
		return 0;
	}

	if (ENABLE_FEATURE_INSTALLER && strcmp(argv[1], "--install") == 0) {
		int use_symbolic_links;
		const char *busybox;

		busybox = xmalloc_readlink(bb_busybox_exec_path);
		if (!busybox) {
			/* bb_busybox_exec_path is usually "/proc/self/exe".
			 * In chroot, readlink("/proc/self/exe") usually fails.
			 * In such case, better use argv[0] as symlink target
			 * if it is a full path name.
			 */
			if (argv[0][0] != '/')
				bb_error_msg_and_die("'%s' is not an absolute path", argv[0]);
			busybox = argv[0];
		}
		/* busybox --install [-s] [DIR]:
		 * -s: make symlinks
		 * DIR: directory to install links to
		 */
		use_symbolic_links = (argv[2] && strcmp(argv[2], "-s") == 0 && argv++);
		install_links(busybox, use_symbolic_links, argv[2]);
		return 0;
	}

	if (strcmp(argv[1], "--help") == 0) {
		/* "busybox --help [<applet>]" */
		if (!argv[2])
			goto help;
		/* convert to "<applet> --help" */
		argv[0] = argv[2];
		argv[2] = NULL;
	} else {
		/* "busybox <applet> arg1 arg2 ..." */
		argv++;
	}
	/* We support "busybox /a/path/to/applet args..." too. Allows for
	 * "#!/bin/busybox"-style wrappers */
	applet_name = bb_get_last_path_component_nostrip(argv[0]);
	run_applet_and_exit(applet_name, argv);

	/*bb_error_msg_and_die("applet not found"); - sucks in printf */
	full_write2_str(applet_name);
	full_write2_str(": applet not found\n");
	xfunc_die();
}

void FAST_FUNC run_applet_no_and_exit(int applet_no, char **argv)
{
	int argc = 1;

	while (argv[argc])
		argc++;

	/* Reinit some shared global data */
	xfunc_error_retval = EXIT_FAILURE;

	applet_name = APPLET_NAME(applet_no);
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		/* Special case. POSIX says "test --help"
		 * should be no different from e.g. "test --foo".  */
//TODO: just compare applet_no with APPLET_NO_test
		if (!ENABLE_TEST || strcmp(applet_name, "test") != 0)
			bb_show_usage();
	}
	if (ENABLE_FEATURE_SUID)
		check_suid(applet_no);
	exit(applet_main[applet_no](argc, argv));
}

void FAST_FUNC run_applet_and_exit(const char *name, char **argv)
{
	int applet = find_applet_by_name(name);
	if (applet >= 0)
		run_applet_no_and_exit(applet, argv);
	if (strncmp(name, "busybox", 7) == 0)
		exit(busybox_main(argv));
}

#endif /* !defined(SINGLE_APPLET_MAIN) */



#if ENABLE_BUILD_LIBBUSYBOX
int lbb_main(char **argv)
#else
int main(int argc UNUSED_PARAM, char **argv)
#endif
{
	/* Tweak malloc for reduced memory consumption */
#ifdef M_TRIM_THRESHOLD
	/* M_TRIM_THRESHOLD is the maximum amount of freed top-most memory
	 * to keep before releasing to the OS
	 * Default is way too big: 256k
	 */
	mallopt(M_TRIM_THRESHOLD, 8 * 1024);
#endif
#ifdef M_MMAP_THRESHOLD
	/* M_MMAP_THRESHOLD is the request size threshold for using mmap()
	 * Default is too big: 256k
	 */
	mallopt(M_MMAP_THRESHOLD, 32 * 1024 - 256);
#endif

#if !BB_MMU
	/* NOMMU re-exec trick sets high-order bit in first byte of name */
	if (argv[0][0] & 0x80) {
		re_execed = 1;
		argv[0][0] &= 0x7f;
	}
#endif

#if defined(SINGLE_APPLET_MAIN)
	/* Only one applet is selected in .config */
	if (argv[1] && strncmp(argv[0], "busybox", 7) == 0) {
		/* "busybox <applet> <params>" should still work as expected */
		argv++;
	}
	/* applet_names in this case is just "applet\0\0" */
	lbb_prepare(applet_names IF_FEATURE_INDIVIDUAL(, argv));
	return SINGLE_APPLET_MAIN(argc, argv);
#else
	lbb_prepare("busybox" IF_FEATURE_INDIVIDUAL(, argv));

	applet_name = argv[0];
	if (applet_name[0] == '-')
		applet_name++;
	if (ENABLE_PLATFORM_MINGW32) {
		const char *applet_name_env = getenv("BUSYBOX_APPLET_NAME");
		if (applet_name_env && *applet_name_env) {
			applet_name = applet_name_env;
			unsetenv("BUSYBOX_APPLET_NAME");
		}
		else {
			int i, len = strlen(applet_name);
			if (len > 4 && !strcmp(applet_name+len-4, ".exe")) {
				len -= 4;
				argv[0][applet_name-argv[0]+len] = '\0';
			}
			for (i = 0; i < len; i++)
				argv[0][applet_name-argv[0]+i] = tolower(applet_name[i]);
		}
	}
	applet_name = bb_basename(applet_name);

	parse_config_file(); /* ...maybe, if FEATURE_SUID_CONFIG */

	run_applet_and_exit(applet_name, argv);

	/*bb_error_msg_and_die("applet not found"); - sucks in printf */
	full_write2_str(applet_name);
	full_write2_str(": applet not found\n");
	xfunc_die();
#endif
}
