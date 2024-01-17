#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void
usage(void)
{
  (void)fprintf(stderr, "usage: nomem --percent=%%mem [--rate=]\n");
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
	long rate = 1;
	static struct option longopts[] = {
		{ "percent", required_argument, NULL, 'p' },
		{ "rate", required_argument, NULL, 'w' },
		{ NULL,   0,                 NULL,  0  },
	};
	while ((ch = getopt_long(argc, argv, "p:w:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'p':
			percent = tolong(optarg);
			if (percent == -1) {
				fputs("invalid argument: percent\n", stderr);
				exit(EXIT_FAILURE);
			}
			break;
		case 'w':
			rate = tolong(optarg);
			if (rate == -1) {
				fputs("invalid argument: rate\n", stderr);
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

	long sz = sysconf(_SC_PAGESIZE);
	long npg = sysconf(_SC_PHYS_PAGES);

	void *p = calloc(percent, npg * sz / 100);
	if (!p) {
		fputs("no memory\n", stderr);
		exit(EXIT_FAILURE);
	}

	sleep(8);
	return EXIT_SUCCESS;
}