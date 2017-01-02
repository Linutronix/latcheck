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

#ifndef SUBPATTERN_H
#define SUBPATTERN_H

#include <time.h>
#include <sys/types.h>
#include <sys/queue.h>

struct subpattern_definition;
struct subpattern_instance;

enum subpattern_boundary {
	in = 0,
	out,
};

struct subpattern_ops {
	int (*enable)(const char *tracingpath, pid_t task);
	void *(*match)(const char *traceline, pid_t task,
		       enum subpattern_boundary bound, void *inbound_data);
	int (*is_relevant)(pid_t task, void *data);
	int (*sched_out)(pid_t task, void *data);
	void (*print)(void *data);
	void (*free_data)(void *data);
	void (*unregister)(struct subpattern_definition *def);
};

struct subpattern_definition {
	void *data;
	struct subpattern_ops *ops;
	int has_sched_switch;

	int id;
	LIST_ENTRY(subpattern_definition) list;
};

extern int register_subpattern(struct subpattern_definition *def);

#endif /* SUBPATTERN_H */
