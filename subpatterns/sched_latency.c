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
	pid_t running_task;
	pid_t task;
	const char *event;
	int in;
};

#define IN_EVENT_STR " sched_wakeup: "
#define IN_PID_STR " pid="

#define OUT_EVENT_STR " sched_switch: "
#define OUT_PID_STR " next_pid="

static int sp_enable(const char *tracingpath, pid_t task)
{
	char filter[64];
	int ret = 0;

	ret |= set_tracing(tracingpath,
			   "events/sched/sched_wakeup/enable", "1\n");

	ret |= set_tracing(tracingpath,
			   "events/sched/sched_switch/enable", "1\n");

	snprintf(filter, sizeof(filter), "pid == %u\n", task);
	ret |= set_tracing(tracingpath, "events/sched/sched_wakeup/filter",
			   filter);

	snprintf(filter, sizeof(filter), "next_pid == %u\n", task);
	ret |= set_tracing(tracingpath, "events/sched/sched_switch/filter",
			   filter);

	return ret;
}

static void *sp_match(const char *traceline, pid_t task,
		      enum subpattern_boundary bound, void *inbound_data)
{
	struct sb_data *in_d = inbound_data;
	const char *event = "";
	struct sb_data *d;
	pid_t target_task;
	char *pid_str;

	switch (bound) {
	case in:
		if (!strstr(traceline, IN_EVENT_STR))
			return NULL;
		event = IN_EVENT_STR;
		pid_str = strstr(traceline, IN_PID_STR);
		if (pid_str)
			pid_str += strlen(IN_PID_STR);
		break;
	case out:
		if (!strstr(traceline, OUT_EVENT_STR))
			return NULL;
		event = OUT_EVENT_STR;
		pid_str = strstr(traceline, OUT_PID_STR);
		if (pid_str)
			pid_str += strlen(OUT_PID_STR);
		break;
	}

	if (!pid_str)
		return NULL;
	target_task = strtoul(pid_str, NULL, 10);

	if (in_d && in_d->task != target_task)
		return NULL;

	d = calloc(1, sizeof(*d));
	if (!d) {
		fprintf(stderr, "calloc failed: %s\n", strerror(errno));
		return NULL;
	}

	d->task = target_task;
	d->running_task = task;
	d->event = event;
	d->in = (bound == in);

	return d;
}

static int sp_is_relevant(pid_t task, void *data)
{
	struct sb_data *d = data;

	if (d->in && d->running_task == task)
		return 1;

	return (d->task == task);
}

static int sp_sched_out(pid_t task, void *data)
{
	struct sb_data *d = data;

	/* we are only interested in the focus task */
	if (d->task != task)
		return 0;

	/*
	 * If it is a wake (in) for the focus task, then
	 * the focus task is currently not scheduled.
	 */
	if (d->in)
		return 1;

	/*
	 * It is a sched_switch (out) to the focus task.
	 * The focus task is now scheduled.
	 */
	return -1;
}

static void sp_print(void *data)
{
	struct sb_data *d = data;

	printf("sched_latency:%s%stask=%u", d->in ? "in" : "out",
	       d->event, d->task);
}

static void sp_free_data(void *data)
{
	free(data);
}

static struct subpattern_ops sp_ops = {
	.enable = sp_enable,
	.match = sp_match,
	.is_relevant = sp_is_relevant,
	.sched_out = sp_sched_out,
	.print = sp_print,
	.free_data = sp_free_data,
};

static struct subpattern_definition sp_def = {
	.data = NULL,
	.ops = &sp_ops,
	.has_sched_switch = 1,
};

int register_sched_latency(void)
{
	return register_subpattern(&sp_def);
}
