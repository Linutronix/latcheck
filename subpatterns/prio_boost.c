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
	pid_t booster;
	unsigned int oldprio;
	unsigned int newprio;
	int in;
};

#define EVENT_STR " sched_pi_setprio: "
#define PID_STR " pid="
#define OLDPRIO_STR " oldprio="
#define NEWPRIO_STR " newprio="

static int sp_enable(const char *tracingpath, pid_t task)
{
	char filter[64];
	int ret = 0;

	ret |= set_tracing(tracingpath,
			   "events/sched/sched_pi_setprio/enable", "1\n");

	snprintf(filter, sizeof(filter), "pid == %u\n", task);
	ret |= set_tracing(tracingpath, "events/sched/sched_pi_setprio/filter",
			   filter);

	return ret;
}

static void *sp_match(const char *traceline, pid_t task,
		      enum subpattern_boundary bound, void *inbound_data)
{
	struct sb_data *in_d = inbound_data;
	unsigned int newprio;
	unsigned int oldprio;
	struct sb_data *d;
	pid_t target_task;
	char *pid_str;

	if (!strstr(traceline, EVENT_STR))
		return NULL;

	pid_str = strstr(traceline, PID_STR);
	if (!pid_str)
		return NULL;
	pid_str += strlen(PID_STR);
	target_task = strtoul(pid_str, NULL, 10);

	pid_str = strstr(traceline, OLDPRIO_STR);
	if (!pid_str)
		return NULL;
	pid_str += strlen(OLDPRIO_STR);
	oldprio = strtoul(pid_str, NULL, 10);

	pid_str = strstr(traceline, NEWPRIO_STR);
	if (!pid_str)
		return NULL;
	pid_str += strlen(NEWPRIO_STR);
	newprio = strtoul(pid_str, NULL, 10);

	switch (bound) {
	case in:
		if (oldprio <= newprio)
			return NULL;
		break;
	case out:
		if (oldprio >= newprio)
			return NULL;
		break;
	}

	if (in_d && in_d->task != target_task)
		return NULL;

	d = calloc(1, sizeof(*d));
	if (!d) {
		fprintf(stderr, "calloc failed: %s\n", strerror(errno));
		return NULL;
	}

	d->task = target_task;
	d->booster = task;
	d->oldprio = oldprio;
	d->newprio = newprio;
	d->in = (bound == in);

	return d;
}

static int sp_is_relevant(pid_t task, void *data)
{
	struct sb_data *d = data;

	if (d->booster == task)
		return 1;

	return (d->task == task);
}

static void sp_print(void *data)
{
	struct sb_data *d = data;
	int oldprio;
	int newprio;

	if (d->oldprio < 99)
		oldprio = 99 - d->oldprio;
	else
		oldprio = 0;

	if (d->newprio < 99)
		newprio = 99 - d->newprio;
	else
		newprio = 0;

	printf("prio_boost:%s%stask=%u prio=%u->%u", d->in ? "in" : "out",
	       EVENT_STR, d->task, oldprio, newprio);
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
	.has_sched_switch = 1,
};

int register_prio_boost(void)
{
	return register_subpattern(&sp_def);
}
