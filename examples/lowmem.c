/*
 * Nomem allocate increasingly large chunks of memory to reach the target in
 * percent, at a rate of 10% of the total memory every 2 seconds.
 */

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "icc.h"

void
usage(void)
{
  (void)fprintf(stderr, "usage: nomem --percent=%%mem\n");
  exit(EXIT_FAILURE);
}

long
tolong(const char *s) {
	char *end;
	unsigned long res;

	res = strtoul(optarg, &end, 0);
	if (errno != 0 || end == s || *end != '\0') {
		return -1;
	}

	if (res > LONG_MAX) {
		return -1;
	}

	return res;
}

int
main(int argc, char **argv) {
	int ch;
	long percent = 0;
	static struct option longopts[] = {
		{ "percent", required_argument, NULL, 'p' },
		{ NULL,   0,                 NULL,  0  },
	};
	while ((ch = getopt_long(argc, argv, "p:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'p':
			percent = tolong(optarg);
			if (percent == -1) {
				fputs("invalid argument: percent\n", stderr);
				exit(EXIT_FAILURE);
			}
			break;
		case 0:
			continue;
		default:
			usage();
		}
	}

	if (percent == 0) {
		usage();
	}

	struct icc_context *icc;
	icc_init(ICC_LOG_DEBUG, 0, &icc);
	assert(icc);

	long sz = sysconf(_SC_PAGESIZE);
	long npg = sysconf(_SC_PHYS_PAGES);
	void *m = NULL;
	bool lowmem = false;
	for (long p = 10; p <= percent; p += 10, sleep(2)) {
		icc_lowmem_pending(icc, &lowmem);
		if (!lowmem) {
			continue;
		}

		void *t = reallocarray(m, p, npg * sz / 100);
		if (!t) {
			fputs("no memory\n", stderr);
			exit(EXIT_FAILURE);
		}
		m = t;
		/* overflow was checked by reallocarray */
		memset(m, 1, p * npg * sz /100);
	}

	icc_fini(icc);

	return EXIT_SUCCESS;
}
