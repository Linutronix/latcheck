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
#include <errno.h>
#include "util.h"
#include "subpattern.h"

struct sb_data {
	pid_t task;
	unsigned int nr;
	unsigned int futex_cmd;
	const char *event;
	int in;
};

#define IN_EVENT_STR " sys_enter: "

#define OUT_EVENT_STR " sys_exit: "

static int sp_enable(const char *tracingpath, pid_t task)
{
	char filter[64];
	int ret = 0;

	ret |= set_tracing(tracingpath,
			   "events/raw_syscalls/sys_enter/enable", "1\n");

	ret |= set_tracing(tracingpath,
			   "events/raw_syscalls/sys_exit/enable", "1\n");

	snprintf(filter, sizeof(filter), "common_pid == %u\n", task);
	ret |= set_tracing(tracingpath, "events/raw_syscalls/sys_enter/filter",
			   filter);

	snprintf(filter, sizeof(filter), "common_pid == %u\n", task);
	ret |= set_tracing(tracingpath, "events/raw_syscalls/sys_exit/filter",
			   filter);

	return ret;
}

static const char *futex_cmd[] = {
	"FUTEX_WAIT",
	"FUTEX_WAKE",
	"FUTEX_FD",
	"FUTEX_REQUEUE",
	"FUTEX_CMP_REQUEUE",
	"FUTEX_WAKE_OP",
	"FUTEX_LOCK_PI",
	"FUTEX_UNLOCK_PI",
	"FUTEX_TRYLOCK_PI",
	"FUTEX_WAIT_BITSET",
	"FUTEX_WAKE_BITSET",
	"FUTEX_WAIT_REQUEUE_PI",
	"FUTEX_CMP_REQUEUE_PI"
};
static unsigned int futex_cmd_max = 11;

static void *sp_match(const char *traceline, pid_t task,
		      enum subpattern_boundary bound, void *inbound_data)
{
	struct sb_data *in_d = inbound_data;
	const char *event = "";
	struct sb_data *d;
	char *pid_str;

	switch (bound) {
	case in:
		if (!strstr(traceline, IN_EVENT_STR))
			return NULL;
		event = IN_EVENT_STR;
		break;
	case out:
		if (!strstr(traceline, OUT_EVENT_STR))
			return NULL;
		event = OUT_EVENT_STR;
		break;
	}

	if (in_d && in_d->task != task)
		return NULL;

	pid_str = strstr(traceline, "NR ");
	if (!pid_str)
		return NULL;
	pid_str += 3;

	d = calloc(1, sizeof(*d));
	if (!d) {
		fprintf(stderr, "calloc failed: %s\n", strerror(errno));
		return NULL;
	}

	d->task = task;
	d->nr = strtoul(pid_str, NULL, 10);
	d->futex_cmd = futex_cmd_max + 1;
	d->event = event;
	d->in = (bound == in);

	if (d->nr == 240) {
		pid_str = strstr(traceline, ", ");
		if (pid_str) {
			pid_str += 2;
			d->futex_cmd = strtoul(pid_str, NULL, 10);
		} else if (in_d) {
			d->futex_cmd = in_d->futex_cmd;
		}
	}

	return d;
}

static int sp_is_relevant(pid_t task, void *data)
{
	struct sb_data *d = data;

	return (d->task == task);
}

static void print_syscall(unsigned int nr, unsigned int fcmd)
{
	char line[80];
	char str[16];
	size_t len;
	char *p;
	FILE *f;

	snprintf(str, sizeof(str), "%u ", nr);
	len = strlen(str);

	f = fopen("syscalls.txt", "r");
	if (!f) {
		printf("?");
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(str, line, len) == 0) {
			p = strchr(line, '\n');
			if (p)
				*p = 0;
			printf(line + len);
			if (fcmd <= futex_cmd_max)
				printf("/%s", futex_cmd[fcmd]);
			break;
		}
	}

	fclose(f);
}

static void sp_print(void *data)
{
	struct sb_data *d = data;

	printf("syscall:%s%snr=%d/", d->in ? "in" : "out", d->event, d->nr);
	print_syscall(d->nr, d->futex_cmd);
	printf(" task=%u", d->task);
}

static void sp_free_data(void *data)
{
	free(data);
}

static struct subpattern_ops sp_ops = {
	.enable = sp_enable,
	.match = sp_match,
	.is_relevant = sp_is_relevant,
	.print = sp_print,
	.free_data = sp_free_data,
};

static struct subpattern_definition sp_def = {
	.data = NULL,
	.ops = &sp_ops,
};

int register_syscall(void)
{
	return register_subpattern(&sp_def);
}
