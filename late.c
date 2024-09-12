/*-
 * Copyright (c) 2002-2003, Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/resource.h>
#ifdef __FreeBSD__
#include <sys/rtprio.h>
#endif
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#ifndef __FreeBSD__
#include <sched.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


bool bflag;		/* Settle before test */
bool cflag;		/* Calibrate ? */
bool iflag;		/* Iterate a specific number of times? */
bool nflag;		/* Was '-n' specified? */
bool pflag;		/* Print priority? */
bool uflag;		/* Use SIGUSR1 to start the test after settling */
bool xflag;		/* Print stats once a second. */

int niceval;		/* Nice setting. */
int rsecs;		/* Run for rsecs seconds. */
unsigned int settle_secs;	/* Settle before test this many seconds */

/* Calibration. */
unsigned int leeway = 5;	/* Percents of error allowed during calibration. */
unsigned int cmiter = 8;	/* Max number of attempts during calibration. */

volatile sig_atomic_t start;	/* Can we start? */
volatile sig_atomic_t done;	/* Should we stop? */

/* Internal multiplicator for memcpy() iterations. */
#define INT_MEMCPY_ITERATIONS	4096

/*
 * Interval sets.
 */
struct iset {
	struct timeval	is_max;		/* Maximum timeval for this set. */
	struct timeval	is_total;	/* Total time accumulated. */
	int	is_count;		/* Number of recorded intervals. */
};

void is_init(struct iset *is);
void is_add(struct iset *is, struct timeval *tv);
void is_average(struct iset *is, struct timeval *tv);
void is_max(struct iset *is, struct timeval *tv);
void is_total(struct iset *is, struct timeval *tv);
void is_count(struct iset *is, int *count);

#ifndef timeradd
#define timeradd(tvp, uvp, vvp)						\
do {									\
	(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;			\
	(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;		\
	if ((vvp)->tv_usec >= 1000000) {				\
		(vvp)->tv_sec++;					\
		(vvp)->tv_usec -= 1000000;				\
	}								\
} while (0)
#endif
#ifndef timercmp
#define timercmp(tvp, uvp, cmp)						\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	((tvp)->tv_usec cmp (uvp)->tv_usec) :				\
	((tvp)->tv_sec cmp (uvp)->tv_sec))

#endif
#ifndef timersub
#define timersub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif

struct iset lat_set;
struct iset lat_cur_set;
struct iset work_set;
struct iset work_cur_set;
struct iset sleep_set;

void test_latency(unsigned int microseconds);
void test_latency_report(struct iset *is);
int test_prio(void);

void work_memcpy(unsigned int count);
void work_memcpy_calibrate(unsigned int microseconds);
void work_memcpy_report(struct iset *is);

void cpu_report(struct timeval *wtime);

void finished(int trash);
void usage(void);


void
is_init(struct iset *is)
{
	memset(is, 0, sizeof(*is));
}

void
is_add(struct iset *is, struct timeval *tv)
{
	is->is_count++;

	/* Add this to the total */
	timeradd(&is->is_total, tv, &is->is_total);

	/* See if this value exceeds the max. */
	if (timercmp(tv, &is->is_max, >))
		is->is_max = *tv;
}

void
is_average(struct iset *is, struct timeval *tv)
{
	uint64_t total;

	if (is->is_count == 0) {
		tv->tv_sec = tv->tv_usec = 0;
		return;
	}

	total = is->is_total.tv_usec;
	total += is->is_total.tv_sec * 1000000;

	total /= is->is_count;

	tv->tv_sec = total / 1000000;
	total -= tv->tv_sec * 1000000;
	tv->tv_usec = total;
}

void
is_max(struct iset *is, struct timeval *tv)
{
	*tv = is->is_max;
}

void
is_count(struct iset *is, int *count)
{
	*count = is->is_count;
}

void
is_total(struct iset *is, struct timeval *tv)
{
	*tv = is->is_total;
}

void
tv_print(char *pre, struct timeval *tv)
{
	printf("%s%ld.%06ld\n", pre, tv->tv_sec, tv->tv_usec);
}


void
finished(int trash)
{
	done = 1;
}

void
sigalarm(int trash)
{
	static int sigcount;

	sigcount++;
	alarm(1);

	if (xflag) {
		printf("Stats for second: %d\n", sigcount);

		work_memcpy_report(&work_cur_set);
		is_init(&work_cur_set);

		test_latency_report(&lat_cur_set);
		is_init(&lat_cur_set);
	}
	if (pflag)
		printf("Current priority:\t%d\n", test_prio());

	printf("\n");
}

void
started(int trash)
{
	start = 1;
}

void
usage(void)
{
	fprintf(stderr, "usage: late [-pux] [-a max calibration attempts] "
	    "[-b settle seconds] [-c work us] [-i work loops]\n"
	    "       [-l calibration leeway percents] [-n niceval] "
	    "[-r run seconds] [-s sleep us] [-w work iterations]\n"
	    "Options:\n"
	    "-a: Max calibration attempts (=feedback loop iterations; "
	    "default: 8).\n"
	    "-b: Wait before the test to let priority settle.\n"
	    "-c: Calibrate: Find work iterations to reach the passed duration.\n"
	    "-i: Number of work + sleep loops (not specified: Infinite).\n"
	    "-l: Leeway percents for the calibration (default: 5).\n"
	    "-n: Renice to the passed value (may need privilege)."
	    "-p: Print the current process' priority every second.\n"
	    "-r: Stop running (work + sleep) when duration reached.\n"
	    "-s: Duration of sleep (in us; default: 1s).\n"
	    "-u: Wait for SIGUSR1 to start (work + sleep) loops.\n"
	    "-w: Number of iterations forming a unit of work.\n"
	    "-x: Print work and latency statistics every second.\n");
	exit(EXIT_FAILURE);
}

static unsigned int
str_to_u(const char *str)
{
	char *endptr;
	unsigned long ul;

	errno = 0;
	ul = strtoul(str, &endptr, 0);
	if (errno != 0 || endptr == str || *endptr != 0 || ul > UINT_MAX)
		errx(EXIT_FAILURE, "Bad number trying to parse '%s'.", str);
	return (ul);
}

int
main(int argc, char **argv)
{
	struct timeval wetime;	/* Work end time */
	struct timeval wstime;	/* Work start time */
	struct timeval curtime;	/* Current time. */
	int smicro;	/* Microseconds of sleep */
	unsigned int wmicro;	/* Microseconds of work */
	unsigned int wcount;	/* work count */
	int icount;	/* Iteration count. */
	int c;
	int error;

	smicro = 1000000;	/* 1 second default */
	wmicro = 1000;		/* 1ms default */

	while ((c = getopt(argc, argv, "a:b:c:i:l:n:pr:s:uw:x")) != -1) {
		switch (c) {
		case 'a':
			cmiter = str_to_u(optarg);
			break;
		case 'b':
			settle_secs = str_to_u(optarg);
			break;
		case 'c':
			cflag = true;
			wmicro = str_to_u(optarg);
			break;
		case 'i':
			iflag = true;
			icount = atoi(optarg);
			break;
		case 'l':
			leeway = str_to_u(optarg);
			if (leeway > 100)
				errx(EXIT_FAILURE,
				    "Leeway must be a number of percents.");
			break;
		case 'n':
			nflag = true;
			niceval = atoi(optarg);
			break;
		case 'p':
			pflag = true;
			break;
		case 'r':
			rsecs = atoi(optarg);
			break;
		case 's':
			smicro = atoi(optarg);
			break;
		case 'u':
			uflag = true;
			break;
		case 'w':
			wcount = str_to_u(optarg);
			break;
		case 'x':
			xflag = true;
			break;
		default:
			usage();
		}
	}
	if (cflag) {
#ifdef __FreeBSD__
		struct rtprio rtp = { .type = RTP_PRIO_FIFO,
				      .prio = RTP_PRIO_MAX };

		error = rtprio(RTP_SET, 0, &rtp);
		if (error != 0)
			error = setpriority(PRIO_PROCESS, 0, PRIO_MIN);
#else
		error = setpriority(PRIO_PROCESS, 0, PRIO_MIN);
#endif
		if (error != 0)
			warnx("Could not increase priority, "
			    "calibration results may be less reliable.");
		work_memcpy_calibrate(wmicro);
		exit(EXIT_SUCCESS);
	}

	/*
	 * Initialize our four interval sets.
	 */
	is_init(&lat_set);
	is_init(&lat_cur_set);
	is_init(&work_set);
	is_init(&work_cur_set);

	if (uflag) {
		sigset_t empty_set;
		sigset_t usr1_set;
		sigset_t cur_set;

		sigemptyset(&empty_set);
		sigemptyset(&usr1_set);
		sigaddset(&usr1_set, SIGUSR1);

		sigprocmask(SIG_BLOCK, &usr1_set, &cur_set);
		signal(SIGUSR1, started);
		while (start == 0)
			sigsuspend(&cur_set);
	}

	signal(SIGINT, finished);
	signal(SIGALRM, sigalarm);

	/* Record the time that we start, for the total work time */
	if (gettimeofday(&wstime, NULL) != 0)
		err(EXIT_FAILURE, "gettimeofday");

	/* Sleep to let the priority settle before test */
	if (settle_secs != 0) {
		struct timeval tv;

		sleep(settle_secs);

		/*
		 * We want the amount of time that we were denied slices before
		 * we woke up to be reflected in the wstime.  This is why we
		 * don't just start the timer below.
		 */
		tv.tv_sec = settle_secs;
		tv.tv_usec = 0;
		timeradd(&wstime, &tv, &wstime);
	}

	if (xflag || pflag)
		alarm(1);

	if (nflag) {
		error = setpriority(PRIO_PROCESS, 0, niceval);
		if (error != 0)
			err(EXIT_FAILURE, "Cannot set the nice value.");
	}

	while (done == 0 && (!iflag || icount--)) {
		if (wmicro)
			work_memcpy(wcount);
		if (done == 0 && smicro)
			test_latency(smicro);
		if (rsecs) {
			gettimeofday(&curtime, NULL);
			curtime.tv_sec -= rsecs;
			if (timercmp(&wstime, &curtime, <))
				break;
		}
	}
	/* Compute the total working time */
	if (gettimeofday(&wetime, NULL) != 0)
		err(EXIT_FAILURE, "gettimeofday");
	timersub(&wetime, &wstime, &wetime);

	/* Generate reports */
	test_latency_report(&lat_set);
	work_memcpy_report(&work_set);
	cpu_report(&wetime);

	exit(EXIT_SUCCESS);
}

void
work_memcpy_calibrate(unsigned int micro)
{
	struct timeval stime;	/* Start time */
	struct timeval etime;	/* End time */
	unsigned int rmicro;		/* Current run time */
	unsigned int count;
	unsigned int niter;

	rmicro = 0;
	count = 10000;
	niter = 0;

#define	SCALE		128
	if (micro * SCALE * 100 <= micro)
		errx(EXIT_FAILURE, "Too long duration requested.");
	while (rmicro == 0 || rmicro < (micro * (100 - leeway) *
	    (SCALE - 1) / (SCALE * 100)) ||
	    rmicro > (micro * (100 + leeway) *
	    (SCALE + 1) / (SCALE * 100))) {
		if (niter++ == cmiter)
			errx(EXIT_FAILURE,
			    "Reached calibration attempts limit (%u). "
			    "Change with '-a', and/or use '-l'.", cmiter);

		if (gettimeofday(&stime, NULL) != 0)
			err(EXIT_FAILURE, "gettimeofday");

		work_memcpy(count);

		if (gettimeofday(&etime, NULL) != 0)
			err(EXIT_FAILURE, "gettimeofday");

		/* Figure out how long we worked for */
		timersub(&etime, &stime, &etime);
		rmicro = (etime.tv_sec * 1000000) + etime.tv_usec;
		printf("%u iterations took %u microseconds.\n",
		    count, rmicro);

		if (rmicro == 0) {
			unsigned int new_count = 2 * count;

			if (count >= UINT_MAX / SCALE)
				goto too_many_iter;
			if (new_count * SCALE <= count)
				count = UINT_MAX / SCALE;
			else
				count = new_count;
		} else {
			printf("(%u * %u) / %u = %u\n", count, SCALE,
			    rmicro, (count * SCALE) / rmicro);
			count = (((count * SCALE) / rmicro) * micro) / SCALE;
			if (count == 0)
				errx(EXIT_FAILURE,
				    "Requested duration too short.");
			if (count >= UINT_MAX / SCALE)
				goto too_many_iter;
		}
	};

	printf("Calibration succeeded after %u iterations.\n", niter);
	printf("Calculated count: %u\n", count);
	return;
too_many_iter:
	errx(EXIT_FAILURE,
	    "Calibration failed, too many iterations would be needed.");
}

void
work_memcpy(unsigned int count)
{
	struct timeval stime;	/* Start time */
	struct timeval etime;	/* End time */
	struct timeval dtime;	/* Difference between the above */
	char buf0[4096];
	char buf1[4096];

	if (gettimeofday(&stime, NULL) != 0)
		err(EXIT_FAILURE, "gettimeofday");

	for (unsigned int i = 0; i != count; ++i)
		for (unsigned int j = 0; j != INT_MEMCPY_ITERATIONS; ++j) {
			memcpy(buf0, buf1, 4096);
			asm volatile ("" ::: "memory");
			memcpy(buf1, buf0, 4096);
		}

	if (gettimeofday(&etime, NULL) != 0)
		err(EXIT_FAILURE, "gettimeofday");

	/* Figure out how long we ran for */
	timersub(&etime, &stime, &dtime);

	is_add(&work_set, &dtime);
	is_add(&work_cur_set, &dtime);
}

void
work_memcpy_report(struct iset *is)
{
	struct timeval tv;
	int count;

	printf("Time executing work loop:\n");

	is_max(is, &tv);
	tv_print("\tMax:\t\t", &tv);

	is_average(is, &tv);
	tv_print("\tAverage:\t", &tv);

	is_count(is, &count);
	printf("\tWork Count:\t%d\n", count);
}

void
cpu_report(struct timeval *wtime)
{
	struct timeval cputime;
	struct timeval tv;
	struct rusage ru;
	uint64_t rmicro;
	uint64_t wmicro;
	double pct;

	if (getrusage(RUSAGE_SELF, &ru) != 0)
		err(EXIT_FAILURE, "getrusage");

	printf("CPU Stats:\n");
	tv_print("\tReal Time:\t", wtime);

	timeradd(&ru.ru_utime, &ru.ru_stime, &cputime);
	tv_print("\tCPU Time:\t", &cputime);

	is_total(&sleep_set, &tv);
	tv_print("\tSleep Time:\t", &tv);

	rmicro = (cputime.tv_sec * 1000000) + cputime.tv_usec;
	wmicro = (wtime->tv_sec * 1000000) + wtime->tv_usec;
	pct = ((double)rmicro / (double)wmicro) * 100;

	printf("\t%%CPU:\t\t%.0f\n", pct);
	printf("\tFinal Priority:\t%d\n", test_prio());
	printf("\tNice setting:\t%d\n", niceval);
	printf("\tVoluntary Ctx Switch:\t%ld\n", ru.ru_nvcsw);
	printf("\tInvoluntary Ctx Switch:\t%ld\n", ru.ru_nivcsw);
}

void
test_latency(unsigned int microseconds)
{
	struct timeval stime;	/* Start time */
	struct timeval etime;	/* End time */
	struct timeval dtime;	/* Difference between the above */
	struct timeval utime;	/* Represents usleep time */
	struct timespec ts;

	if (gettimeofday(&stime, NULL) != 0)
		err(EXIT_FAILURE, "gettimeofday");

	ts.tv_sec = microseconds / 1000000;
	ts.tv_nsec = (microseconds - (ts.tv_sec * 1000000)) * 1000;
	while (nanosleep(&ts, &ts) != 0);

	if (gettimeofday(&etime, NULL) != 0)
		err(EXIT_FAILURE, "gettimeofday");

	/* Figure out how long we slept for */
	timersub(&etime, &stime, &dtime);

	/* Add this to the total time spent sleeping. */
	is_add(&sleep_set, &dtime);

	/* Now subtract how long we should have slept for */
	utime.tv_sec = 0;
	utime.tv_usec = microseconds;
	timersub(&dtime, &utime, &dtime);

	/* Add this to the total */
	is_add(&lat_set, &dtime);
	is_add(&lat_cur_set, &dtime);
}

void
test_latency_report(struct iset *is)
{
	struct timeval tv;
	int count;

	printf("Sleep resumption latency:\n");

	is_max(is, &tv);
	tv_print("\tMax:\t\t", &tv);

	is_average(is, &tv);
	tv_print("\tAverage:\t", &tv);

	is_count(is, &count);
	printf("\tSleep Count:\t%d\n", count);
}

int
test_prio()
{
	int priority;
#ifdef __FreeBSD__
	struct rtprio rtp;

	if (rtprio(RTP_LOOKUP, 0, &rtp) != 0)
		err(EXIT_FAILURE, "rtprio");

	priority = rtp.prio;
#else
	struct sched_param sp;

	if (sched_getparam(0, &sp) != 0)
		err(EXIT_FAILURE, "sched_getparam");

	priority = sp.sched_priority;
#endif

	return (priority);
}
