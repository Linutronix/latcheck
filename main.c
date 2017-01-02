/*
 * Copyright (C) 2016-2017 Ericsson AB
 * This file is part of latcheck.
 *
 * latcheck is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * latcheck is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with latcheck.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <mcheck.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

void subpattern_init(const char *tracingpath, pid_t task);
int subpattern_handle_traceline(const char *traceline);
void subpattern_cleanup(void);

int main(int argc, char *argv[])
{
	char tracingpath[256];
	char line[512];
	int pipefd[2];
	pid_t task;
	FILE *f;

	mtrace();

	if (argc < 2) {
		fprintf(stderr, "usage: %s <command> <arg>...\n", argv[0]);
		return 1;
	}

	if (pipe(pipefd) != 0) {
		fprintf(stderr, "pipe failed: %s\n", strerror(errno));
		return 1;
	}

	task = fork();
	if (task == -1) {
		fprintf(stderr, "fork failed: %s\n", strerror(errno));
		return 1;
	}

	if (task == 0) {
		/* child */
		cpu_set_t cset;
		char c;

		close(pipefd[1]);
		while (1) {
			if (read(pipefd[0], &c, 1) == 1) {
				if (c == 'r')
					break;
				exit(1);
			} else if (errno != EINTR) {
				exit(1);
			}
		}

		close(pipefd[0]);

		CPU_ZERO(&cset);
		CPU_SET(0, &cset);

		if (sched_setaffinity(0, sizeof(cset), &cset) != 0)
			exit(1);

		execvp(argv[1], &argv[1]);

		exit(1);
	}

	close(pipefd[0]);

	snprintf(tracingpath, sizeof(tracingpath),
		 "/sys/kernel/debug/tracing/instances/latency_trace.%u", task);

	mkdir(tracingpath, 0700);

	subpattern_init(tracingpath, task);

	snprintf(line, sizeof(line), "%s/tracing_on", tracingpath);
	f = fopen(line, "w");
	if (!f) {
		fprintf(stderr, "fopen failed: %s\n", strerror(errno));
		return 1;
	}

	write(pipefd[1], "r", 1);
	close(pipefd[1]);
	wait(NULL);
	fwrite("0\n", 2, 1, f);
	fclose(f);

	printf("processing task: %u\n", task);

	snprintf(line, sizeof(line), "%s/per_cpu/cpu0/trace", tracingpath);
	f = fopen(line, "r");
	if (!f) {
		fprintf(stderr, "fopen failed: %s\n", strerror(errno));
		return 1;
	}

	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#')
			continue;
		if (subpattern_handle_traceline(line) != 0)
			fprintf(stderr, "parse failed: %s", line);
	}

	fclose(f);

	subpattern_cleanup();

	rmdir(tracingpath);

	return 0;
}
