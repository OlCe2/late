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

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/timespec.h>
#include <sys/resource.h>
#ifdef __FreeBSD__
#include <sys/rtprio.h>
#else
#include <sched.h>
#endif


int bflag;		/* Settle before test */
int cflag;		/* Calibrate ? */
int iflag;		/* Iterate a specific number of times? */
int pflag;		/* Print priority? */
int uflag;		/* Use SIGUSR1 to start the test after settling */
int xflag;		/* Print stats once a second. */
int rsecs;		/* Run for rsecs seconds. */
int niceval;		/* Nice setting. */

int done;		/* Are we there yet? */
int start;		/* Can we go yet? */


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
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;	\
		if ((vvp)->tv_usec >= 1000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_usec -= 1000000;			\
		}							\
} while (0)
#endif
#ifndef timercmp
#define timercmp(tvp, uvp, cmp)						\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
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

void work_memcpy(int count);
int work_memcpy_calibrate(unsigned int microseconds);
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
	errx(EXIT_FAILURE, "usage: lat [-bpquv] [-c work us] [-r run seconds] [-s sleep us] [-w work loops]");
}

int
main(int argc, char **argv)
{
	struct timeval wetime;	/* Work end time */
	struct timeval wstime;	/* Work start time */
	struct timeval curtime;	/* Current time. */
	int smicro;	/* Microseconds of sleep */
	int wmicro;	/* Microseconds of work */
	int wcount;	/* work count */
	int icount;	/* Iteration count. */
	int c;

	smicro = 1000000;	/* 1 second default */
	wmicro = 1000;		/* 1ms default */

	while ((c = getopt(argc, argv, "bc:i:n:pr:s:uw:x")) != -1) {
		switch (c) {
			case 'b':
				bflag = 1;
				break;
			case 'c':
				cflag = 1;
				wmicro = atoi(optarg);
				break;
			case 'i':
				iflag = 1;
				icount = atoi(optarg);
				break;
			case 'n':
				niceval = atoi(optarg);
				break;
			case 'p':
				pflag = 1;
				break;
			case 'r':
				rsecs = atoi(optarg);
				break;
			case 's':
				smicro = atoi(optarg);
				break;
			case 'u':
				uflag = 1;
				break;
			case 'w':
				wcount = atoi(optarg);
				break;
			case 'x':
				xflag = 1;
				break;
			default:
				usage();
		}
	}
	if ((cflag || niceval) && geteuid() != 0) {
		printf("%s must be run as root with the c or n options set!\n",
		    argv[0]);
		exit(EXIT_FAILURE);
	}
	if (cflag) {
		nice(-20);
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
		sigset_t sigs;
		
		sigemptyset(&sigs);
		signal(SIGUSR1, started);
		while (start == 0)
			sigsuspend(&sigs);
	}

	signal(SIGINT, finished);
	signal(SIGALRM, finished);
	signal(SIGALRM, sigalarm);

	/* Record the time that we start, for the total work time */
	if (gettimeofday(&wstime, NULL) != 0)
		err(EXIT_FAILURE, NULL);

	/* Sleep 2seconds to let the priority settle before test */
	if (bflag) {
		struct timeval tv;

		sleep(2);

		/*
		 * We want the amount of time that we were denied slices
		 * before we woke up to be reflected in the wstime.  This is
		 * why we don't just start the timer below.
		 */
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		timeradd(&wstime, &tv, &wstime);
	}

	if (xflag || pflag)
		alarm(1);

	nice(niceval);

	while (!done && (!iflag || icount--)) {
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
		err(EXIT_FAILURE, NULL);
	timersub(&wetime, &wstime, &wetime);

	/* Generate reports */
	test_latency_report(&lat_set);
	work_memcpy_report(&work_set);
	cpu_report(&wetime);

	exit(EXIT_SUCCESS);
}

int
work_memcpy_calibrate(unsigned int micro)
{
	struct timeval stime;	/* Start time */
	struct timeval etime;	/* End time */
	unsigned int rmicro;		/* Current run time */
	int count;

	rmicro = 0;
	count = 1000;

	while ((rmicro < ((micro / 10) * 9) ||
	    rmicro > ((micro / 10) * 11))) {

#define	SCALE	100
		if (rmicro)
			count = (((count * SCALE) / rmicro) * micro) / SCALE;

		if (gettimeofday(&stime, NULL) != 0)
			err(EXIT_FAILURE, NULL);

		work_memcpy(count);

		if (gettimeofday(&etime, NULL) != 0)
			err(EXIT_FAILURE, NULL);

		/* Figure out how long we worked for */
		timersub(&etime, &stime, &etime);
		rmicro = (etime.tv_sec * 1000000) + etime.tv_usec;
		printf("work_memcpy %d takes %d microseconds.\n",
		    count, rmicro);
		printf("(%d * %d) / %d = %d\n", count, SCALE, rmicro,
		    (count * SCALE) / rmicro);
	} 

	printf("Calculated count: %d\n", count);

	return (count);
}

void
work_memcpy(int count)
{
	struct timeval stime;	/* Start time */
	struct timeval etime;	/* End time */
	struct timeval dtime;	/* Difference between the above */
	char buf0[4096];
	char buf1[4096];

	if (gettimeofday(&stime, NULL) != 0)
		err(EXIT_FAILURE, NULL);

	for (; count > 0; count--) {
		memcpy(buf0, buf1, 4096);
		memcpy(buf1, buf0, 4096);
	}

	if (gettimeofday(&etime, NULL) != 0)
		err(EXIT_FAILURE, NULL);

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
		err(EXIT_FAILURE, NULL);

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
		err(EXIT_FAILURE, NULL);

#if 0
	/*
	 * We could receive a signal while doing this.  We have to
	 * recalculate microseconds and then retry.
	 */
	while (usleep(microseconds) != 0) {
		uint64_t micro;

		if (gettimeofday(&etime, NULL) != 0)
			err(EXIT_FAILURE, NULL);

		/* Figure out how long we slept for */
		timersub(&etime, &stime, &dtime);
		micro = (dtime.tv_sec * 1000000) + dtime.tv_usec;
		if (micro >= microseconds)
			break;
		microseconds -= micro;
	}
#else
	ts.tv_sec = microseconds / 1000000;
	ts.tv_nsec = (microseconds - (ts.tv_sec * 1000000)) * 1000;
	while (nanosleep(&ts, &ts) != 0);
#endif

	if (gettimeofday(&etime, NULL) != 0)
		err(EXIT_FAILURE, NULL);

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
		err(EXIT_FAILURE, NULL);

	priority = rtp.prio;
#else
	struct sched_param sp;

	if (sched_getparam(0, &sp) != 0)
		err(EXIT_FAILURE, NULL);

	priority = sp.sched_priority;
#endif

	return (priority);
}
