/* vi: set sw=4 ts=4: */
/*
 * Mini ps implementation(s) for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Fix for SELinux Support:(c)2007 Hiroshi Shinji <shiroshi@my.email.ne.jp>
 *                         (c)2007 Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

//usage:#if ENABLE_DESKTOP
//usage:
//usage:#define ps_trivial_usage
//usage:       "[-o COL1,COL2=HEADER]" IF_FEATURE_SHOW_THREADS(" [-T]")
//usage:#define ps_full_usage "\n\n"
//usage:       "Show list of processes\n"
//usage:     "\n	-o COL1,COL2=HEADER	Select columns for display"
//usage:	IF_FEATURE_SHOW_THREADS(
//usage:     "\n	-T			Show threads"
//usage:	)
//usage:
//usage:#else /* !ENABLE_DESKTOP */
//usage:
//usage:#if !ENABLE_SELINUX && !ENABLE_FEATURE_PS_WIDE
//usage:#define USAGE_PS "\nThis version of ps accepts no options"
//usage:#else
//usage:#define USAGE_PS ""
//usage:#endif
//usage:
//usage:#define ps_trivial_usage
//usage:       ""
//usage:#define ps_full_usage "\n\n"
//usage:       "Show list of processes\n"
//usage:	USAGE_PS
//usage:	IF_SELINUX(
//usage:     "\n	-Z	Show selinux context"
//usage:	)
//usage:	IF_FEATURE_PS_WIDE(
//usage:     "\n	w	Wide output"
//usage:	)
//usage:
//usage:#endif /* ENABLE_DESKTOP */
//usage:
//usage:#define ps_example_usage
//usage:       "$ ps\n"
//usage:       "  PID  Uid      Gid State Command\n"
//usage:       "    1 root     root     S init\n"
//usage:       "    2 root     root     S [kflushd]\n"
//usage:       "    3 root     root     S [kupdate]\n"
//usage:       "    4 root     root     S [kpiod]\n"
//usage:       "    5 root     root     S [kswapd]\n"
//usage:       "  742 andersen andersen S [bash]\n"
//usage:       "  743 andersen andersen S -bash\n"
//usage:       "  745 root     root     S [getty]\n"
//usage:       " 2990 andersen andersen R ps\n"

#include "libbb.h"

/* Absolute maximum on output line length */
enum { MAX_WIDTH = 2*1024 };

#if ENABLE_DESKTOP

#ifdef __linux__
# include <sys/sysinfo.h>
#endif
#include <sys/times.h> /* for times() */
#ifndef AT_CLKTCK
# define AT_CLKTCK 17
#endif

/* TODO:
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html
 * specifies (for XSI-conformant systems) following default columns
 * (l and f mark columns shown with -l and -f respectively):
 * F     l   Flags (octal and additive) associated with the process (??)
 * S     l   The state of the process
 * UID   f,l The user ID; the login name is printed with -f
 * PID       The process ID
 * PPID  f,l The parent process
 * C     f,l Processor utilization
 * PRI   l   The priority of the process; higher numbers mean lower priority
 * NI    l   Nice value
 * ADDR  l   The address of the process
 * SZ    l   The size in blocks of the core image of the process
 * WCHAN l   The event for which the process is waiting or sleeping
 * STIME f   Starting time of the process
 * TTY       The controlling terminal for the process
 * TIME      The cumulative execution time for the process
 * CMD       The command name; the full command line is shown with -f
 */
typedef struct {
	uint16_t width;
	char name6[6];
	const char *header;
	void (*f)(char *buf, int size, const procps_status_t *ps);
	int ps_flags;
} ps_out_t;

struct globals {
	ps_out_t* out;
	int out_cnt;
	int print_header;
	int need_flags;
	char *buffer;
	unsigned terminal_width;
#if ENABLE_FEATURE_PS_TIME
	unsigned kernel_HZ;
	unsigned long long seconds_since_boot;
#endif
} FIX_ALIASING;
#define G (*(struct globals*)&bb_common_bufsiz1)
#define out                (G.out               )
#define out_cnt            (G.out_cnt           )
#define print_header       (G.print_header      )
#define need_flags         (G.need_flags        )
#define buffer             (G.buffer            )
#define terminal_width     (G.terminal_width    )
#define kernel_HZ          (G.kernel_HZ         )
#define seconds_since_boot (G.seconds_since_boot)
#define INIT_G() do { } while (0)

#if ENABLE_FEATURE_PS_TIME
/* for ELF executables, notes are pushed before environment and args */
static ptrdiff_t find_elf_note(ptrdiff_t findme)
{
	ptrdiff_t *ep = (ptrdiff_t *) environ;

	while (*ep++)
		continue;
	while (*ep) {
		if (ep[0] == findme) {
			return ep[1];
		}
		ep += 2;
	}
	return -1;
}

#if ENABLE_FEATURE_PS_UNUSUAL_SYSTEMS
static unsigned get_HZ_by_waiting(void)
{
	struct timeval tv1, tv2;
	unsigned t1, t2, r, hz;
	unsigned cnt = cnt; /* for compiler */
	int diff;

	r = 0;

	/* Wait for times() to reach new tick */
	t1 = times(NULL);
	do {
		t2 = times(NULL);
	} while (t2 == t1);
	gettimeofday(&tv2, NULL);

	do {
		t1 = t2;
		tv1.tv_usec = tv2.tv_usec;

		/* Wait exactly one times() tick */
		do {
			t2 = times(NULL);
		} while (t2 == t1);
		gettimeofday(&tv2, NULL);

		/* Calculate ticks per sec, rounding up to even */
		diff = tv2.tv_usec - tv1.tv_usec;
		if (diff <= 0) diff += 1000000;
		hz = 1000000u / (unsigned)diff;
		hz = (hz+1) & ~1;

		/* Count how many same hz values we saw */
		if (r != hz) {
			r = hz;
			cnt = 0;
		}
		cnt++;
	} while (cnt < 3); /* exit if saw 3 same values */

	return r;
}
#else
static inline unsigned get_HZ_by_waiting(void)
{
	/* Better method? */
	return 100;
}
#endif

static unsigned get_kernel_HZ(void)
{
	//char buf[64];
	struct sysinfo info;

	if (kernel_HZ)
		return kernel_HZ;

	/* Works for ELF only, Linux 2.4.0+ */
	kernel_HZ = find_elf_note(AT_CLKTCK);
	if (kernel_HZ == (unsigned)-1)
		kernel_HZ = get_HZ_by_waiting();

	//if (open_read_close("/proc/uptime", buf, sizeof(buf)) <= 0)
	//	bb_perror_msg_and_die("can't read %s", "/proc/uptime");
	//buf[sizeof(buf)-1] = '\0';
	///sscanf(buf, "%llu", &seconds_since_boot);
	sysinfo(&info);
	seconds_since_boot = info.uptime;

	return kernel_HZ;
}
#endif

/* Print value to buf, max size+1 chars (including trailing '\0') */

static void func_user(char *buf, int size, const procps_status_t *ps)
{
#if 1
	safe_strncpy(buf, get_cached_username(ps->uid), size+1);
#else
	/* "compatible" version, but it's larger */
	/* procps 2.18 shows numeric UID if name overflows the field */
	/* TODO: get_cached_username() returns numeric string if
	 * user has no passwd record, we will display it
	 * left-justified here; too long usernames are shown
	 * as _right-justified_ IDs. Is it worth fixing? */
	const char *user = get_cached_username(ps->uid);
	if (strlen(user) <= size)
		safe_strncpy(buf, user, size+1);
	else
		sprintf(buf, "%*u", size, (unsigned)ps->uid);
#endif
}

static void func_group(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, get_cached_groupname(ps->gid), size+1);
}

static void func_comm(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, ps->comm, size+1);
}

static void func_state(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, ps->state, size+1);
}

static void func_args(char *buf, int size, const procps_status_t *ps)
{
	read_cmdline(buf, size+1, ps->pid, ps->comm);
}

static void func_pid(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*u", size, ps->pid);
}

static void func_ppid(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*u", size, ps->ppid);
}

static void func_pgid(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*u", size, ps->pgid);
}

static void put_lu(char *buf, int size, unsigned long u)
{
	char buf4[5];

	/* see http://en.wikipedia.org/wiki/Tera */
	smart_ulltoa4(u, buf4, " mgtpezy");
	buf4[4] = '\0';
	sprintf(buf, "%.*s", size, buf4);
}

static void func_vsz(char *buf, int size, const procps_status_t *ps)
{
	put_lu(buf, size, ps->vsz);
}

static void func_rss(char *buf, int size, const procps_status_t *ps)
{
	put_lu(buf, size, ps->rss);
}

static void func_tty(char *buf, int size, const procps_status_t *ps)
{
	buf[0] = '?';
	buf[1] = '\0';
	if (ps->tty_major) /* tty field of "0" means "no tty" */
		snprintf(buf, size+1, "%u,%u", ps->tty_major, ps->tty_minor);
}

#if ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS

static void func_rgroup(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, get_cached_groupname(ps->rgid), size+1);
}

static void func_ruser(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, get_cached_username(ps->ruid), size+1);
}

static void func_nice(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*d", size, ps->niceness);
}

#endif

#if ENABLE_FEATURE_PS_TIME

static void func_etime(char *buf, int size, const procps_status_t *ps)
{
	/* elapsed time [[dd-]hh:]mm:ss; here only mm:ss */
	unsigned long mm;
	unsigned ss;

	mm = ps->start_time / get_kernel_HZ();
	/* must be after get_kernel_HZ()! */
	mm = seconds_since_boot - mm;
	ss = mm % 60;
	mm /= 60;
	snprintf(buf, size+1, "%3lu:%02u", mm, ss);
}

static void func_time(char *buf, int size, const procps_status_t *ps)
{
	/* cumulative time [[dd-]hh:]mm:ss; here only mm:ss */
	unsigned long mm;
	unsigned ss;

	mm = (ps->utime + ps->stime) / get_kernel_HZ();
	ss = mm % 60;
	mm /= 60;
	snprintf(buf, size+1, "%3lu:%02u", mm, ss);
}

#endif

#if ENABLE_SELINUX
static void func_label(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, ps->context ? ps->context : "unknown", size+1);
}
#endif

/*
static void func_nice(char *buf, int size, const procps_status_t *ps)
{
	ps->???
}

static void func_pcpu(char *buf, int size, const procps_status_t *ps)
{
}
*/

static const ps_out_t out_spec[] = {
/* Mandated by http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html: */
#if !ENABLE_PLATFORM_MINGW32
	{ 8                  , "user"  ,"USER"   ,func_user  ,PSSCAN_UIDGID  },
	{ 8                  , "group" ,"GROUP"  ,func_group ,PSSCAN_UIDGID  },
#endif
	{ 16                 , "comm"  ,"COMMAND",func_comm  ,PSSCAN_COMM    },
#if !ENABLE_PLATFORM_MINGW32
	{ MAX_WIDTH          , "args"  ,"COMMAND",func_args  ,PSSCAN_COMM    },
#endif
	{ 5                  , "pid"   ,"PID"    ,func_pid   ,PSSCAN_PID     },
#if !ENABLE_PLATFORM_MINGW32
	{ 5                  , "ppid"  ,"PPID"   ,func_ppid  ,PSSCAN_PPID    },
	{ 5                  , "pgid"  ,"PGID"   ,func_pgid  ,PSSCAN_PGID    },
#endif
#if ENABLE_FEATURE_PS_TIME
	{ sizeof("ELAPSED")-1, "etime" ,"ELAPSED",func_etime ,PSSCAN_START_TIME },
#endif
#if ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS
	{ 5                  , "nice"  ,"NI"     ,func_nice  ,PSSCAN_NICE    },
	{ 8                  , "rgroup","RGROUP" ,func_rgroup,PSSCAN_RUIDGID },
	{ 8                  , "ruser" ,"RUSER"  ,func_ruser ,PSSCAN_RUIDGID },
//	{ 5                  , "pcpu"  ,"%CPU"   ,func_pcpu  ,PSSCAN_        },
#endif
#if ENABLE_FEATURE_PS_TIME
	{ 6                  , "time"  ,"TIME"   ,func_time  ,PSSCAN_STIME | PSSCAN_UTIME },
#endif
#if !ENABLE_PLATFORM_MINGW32
	{ 6                  , "tty"   ,"TT"     ,func_tty   ,PSSCAN_TTY     },
	{ 4                  , "vsz"   ,"VSZ"    ,func_vsz   ,PSSCAN_VSZ     },
/* Not mandated, but useful: */
	{ 4                  , "stat"  ,"STAT"   ,func_state ,PSSCAN_STATE   },
	{ 4                  , "rss"   ,"RSS"    ,func_rss   ,PSSCAN_RSS     },
#endif
#if ENABLE_SELINUX
	{ 35                 , "label" ,"LABEL"  ,func_label ,PSSCAN_CONTEXT },
#endif
};

static ps_out_t* new_out_t(void)
{
	out = xrealloc_vector(out, 2, out_cnt);
	return &out[out_cnt++];
}

static const ps_out_t* find_out_spec(const char *name)
{
	unsigned i;
	char buf[ARRAY_SIZE(out_spec)*7 + 1];
	char *p = buf;

	for (i = 0; i < ARRAY_SIZE(out_spec); i++) {
		if (strncmp(name, out_spec[i].name6, 6) == 0)
			return &out_spec[i];
		p += sprintf(p, "%.6s,", out_spec[i].name6);
	}
	p[-1] = '\0';
	bb_error_msg_and_die("bad -o argument '%s', supported arguments: %s", name, buf);
}

static void parse_o(char* opt)
{
	ps_out_t* new;
	// POSIX: "-o is blank- or comma-separated list" (FIXME)
	char *comma, *equal;
	while (1) {
		comma = strchr(opt, ',');
		equal = strchr(opt, '=');
		if (comma && (!equal || equal > comma)) {
			*comma = '\0';
			*new_out_t() = *find_out_spec(opt);
			*comma = ',';
			opt = comma + 1;
			continue;
		}
		break;
	}
	// opt points to last spec in comma separated list.
	// This one can have =HEADER part.
	new = new_out_t();
	if (equal)
		*equal = '\0';
	*new = *find_out_spec(opt);
	if (equal) {
		*equal = '=';
		new->header = equal + 1;
		// POSIX: the field widths shall be ... at least as wide as
		// the header text (default or overridden value).
		// If the header text is null, such as -o user=,
		// the field width shall be at least as wide as the
		// default header text
		if (new->header[0]) {
			new->width = strlen(new->header);
			print_header = 1;
		}
	} else
		print_header = 1;
}

static void alloc_line_buffer(void)
{
	int i;
	int width = 0;
	for (i = 0; i < out_cnt; i++) {
		need_flags |= out[i].ps_flags;
		if (out[i].header[0]) {
			print_header = 1;
		}
		width += out[i].width + 1; /* "FIELD " */
		if ((int)(width - terminal_width) > 0) {
			/* The rest does not fit on the screen */
			//out[i].width -= (width - terminal_width - 1);
			out_cnt = i + 1;
			break;
		}
	}
#if ENABLE_SELINUX
	if (!is_selinux_enabled())
		need_flags &= ~PSSCAN_CONTEXT;
#endif
	buffer = xmalloc(width + 1); /* for trailing \0 */
}

static void format_header(void)
{
	int i;
	ps_out_t* op;
	char *p;

	if (!print_header)
		return;
	p = buffer;
	i = 0;
	if (out_cnt) {
		while (1) {
			op = &out[i];
			if (++i == out_cnt) /* do not pad last field */
				break;
			p += sprintf(p, "%-*s ", op->width, op->header);
		}
		strcpy(p, op->header);
	}
	printf("%.*s\n", terminal_width, buffer);
}

static void format_process(const procps_status_t *ps)
{
	int i, len;
	char *p = buffer;
	i = 0;
	if (out_cnt) while (1) {
		out[i].f(p, out[i].width, ps);
		// POSIX: Any field need not be meaningful in all
		// implementations. In such a case a hyphen ( '-' )
		// should be output in place of the field value.
		if (!p[0]) {
			p[0] = '-';
			p[1] = '\0';
		}
		len = strlen(p);
		p += len;
		len = out[i].width - len + 1;
		if (++i == out_cnt) /* do not pad last field */
			break;
		p += sprintf(p, "%*s", len, "");
	}
	printf("%.*s\n", terminal_width, buffer);
}

#if ENABLE_SELINUX
# define SELINUX_O_PREFIX "label,"
# define DEFAULT_O_STR    (SELINUX_O_PREFIX "pid,user" IF_FEATURE_PS_TIME(",time") ",args")
#elif ENABLE_PLATFORM_MINGW32
# define DEFAULT_O_STR    ("pid,comm")
#else
# define DEFAULT_O_STR    ("pid,user" IF_FEATURE_PS_TIME(",time") ",args")
#endif

int ps_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ps_main(int argc UNUSED_PARAM, char **argv)
{
	procps_status_t *p;
	llist_t* opt_o = NULL;
	char default_o[sizeof(DEFAULT_O_STR)];
	int opt;
	enum {
		OPT_Z = (1 << 0),
		OPT_o = (1 << 1),
		OPT_a = (1 << 2),
		OPT_A = (1 << 3),
		OPT_d = (1 << 4),
		OPT_e = (1 << 5),
		OPT_f = (1 << 6),
		OPT_l = (1 << 7),
		OPT_T = (1 << 8) * ENABLE_FEATURE_SHOW_THREADS,
	};

	INIT_G();

	// POSIX:
	// -a  Write information for all processes associated with terminals
	//     Implementations may omit session leaders from this list
	// -A  Write information for all processes
	// -d  Write information for all processes, except session leaders
	// -e  Write information for all processes (equivalent to -A)
	// -f  Generate a full listing
	// -l  Generate a long listing
	// -o col1,col2,col3=header
	//     Select which columns to display
	/* We allow (and ignore) most of the above. FIXME.
	 * -T is picked for threads (POSIX hasn't it standardized).
	 * procps v3.2.7 supports -T and shows tids as SPID column,
	 * it also supports -L where it shows tids as LWP column.
	 */
	opt_complementary = "o::";
	opt = getopt32(argv, "Zo:aAdefl"IF_FEATURE_SHOW_THREADS("T"), &opt_o);
	if (opt_o) {
		do {
			parse_o(llist_pop(&opt_o));
		} while (opt_o);
	} else {
		/* Below: parse_o() needs char*, NOT const char*, can't give it default_o */
#if ENABLE_SELINUX
		if (!(opt & OPT_Z) || !is_selinux_enabled()) {
			/* no -Z or no SELinux: do not show LABEL */
			strcpy(default_o, DEFAULT_O_STR + sizeof(SELINUX_O_PREFIX)-1);
		} else
#endif
		{
			strcpy(default_o, DEFAULT_O_STR);
		}
		parse_o(default_o);
	}
#if ENABLE_FEATURE_SHOW_THREADS
	if (opt & OPT_T)
		need_flags |= PSSCAN_TASKS;
#endif

	/* Was INT_MAX, but some libc's go belly up with printf("%.*s")
	 * and such large widths */
	terminal_width = MAX_WIDTH;
	if (isatty(1)) {
		get_terminal_width_height(0, &terminal_width, NULL);
		if (--terminal_width > MAX_WIDTH)
			terminal_width = MAX_WIDTH;
	}
	alloc_line_buffer();
	format_header();

	p = NULL;
	while ((p = procps_scan(p, need_flags)) != NULL) {
		format_process(p);
	}

	return EXIT_SUCCESS;
}


#else /* !ENABLE_DESKTOP */


int ps_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ps_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	procps_status_t *p;
	int psscan_flags = PSSCAN_PID | PSSCAN_UIDGID
			| PSSCAN_STATE | PSSCAN_VSZ | PSSCAN_COMM;
	unsigned terminal_width IF_NOT_FEATURE_PS_WIDE(= 79);
	enum {
		OPT_Z = (1 << 0) * ENABLE_SELINUX,
		OPT_T = (1 << ENABLE_SELINUX) * ENABLE_FEATURE_SHOW_THREADS,
	};
	int opts = 0;
	/* If we support any options, parse argv */
#if ENABLE_SELINUX || ENABLE_FEATURE_SHOW_THREADS || ENABLE_FEATURE_PS_WIDE
# if ENABLE_FEATURE_PS_WIDE
	/* -w is a bit complicated */
	int w_count = 0;
	opt_complementary = "-:ww";
	opts = getopt32(argv, IF_SELINUX("Z")IF_FEATURE_SHOW_THREADS("T")"w", &w_count);
	/* if w is given once, GNU ps sets the width to 132,
	 * if w is given more than once, it is "unlimited"
	 */
	if (w_count) {
		terminal_width = (w_count == 1) ? 132 : MAX_WIDTH;
	} else {
		get_terminal_width_height(0, &terminal_width, NULL);
		/* Go one less... */
		if (--terminal_width > MAX_WIDTH)
			terminal_width = MAX_WIDTH;
	}
# else
	/* -w is not supported, only -Z and/or -T */
	opt_complementary = "-";
	opts = getopt32(argv, IF_SELINUX("Z")IF_FEATURE_SHOW_THREADS("T"));
# endif
#endif

#if ENABLE_SELINUX
	if ((opts & OPT_Z) && is_selinux_enabled()) {
		psscan_flags = PSSCAN_PID | PSSCAN_CONTEXT
				| PSSCAN_STATE | PSSCAN_COMM;
		puts("  PID CONTEXT                          STAT COMMAND");
	} else
#endif
	{
		puts("  PID USER       VSZ STAT COMMAND");
	}
	if (opts & OPT_T) {
		psscan_flags |= PSSCAN_TASKS;
	}

	p = NULL;
	while ((p = procps_scan(p, psscan_flags)) != NULL) {
		int len;
#if ENABLE_SELINUX
		if (psscan_flags & PSSCAN_CONTEXT) {
			len = printf("%5u %-32.32s %s  ",
					p->pid,
					p->context ? p->context : "unknown",
					p->state);
		} else
#endif
		{
			const char *user = get_cached_username(p->uid);
			//if (p->vsz == 0)
			//	len = printf("%5u %-8.8s        %s ",
			//		p->pid, user, p->state);
			//else
			{
				char buf6[6];
				smart_ulltoa5(p->vsz, buf6, " mgtpezy");
				buf6[5] = '\0';
				len = printf("%5u %-8.8s %s %s  ",
					p->pid, user, buf6, p->state);
			}
		}

		{
			int sz = terminal_width - len;
			char buf[sz + 1];
			read_cmdline(buf, sz, p->pid, p->comm);
			puts(buf);
		}
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		clear_username_cache();
	return EXIT_SUCCESS;
}

#endif /* !ENABLE_DESKTOP */
