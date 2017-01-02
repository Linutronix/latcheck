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
#include "subpatterns/subpatterns.h"
#include "subpattern.h"

#define TERM_RESET() printf("\e[0m")
#define TERM_CURSOR_END() printf("\e[K")
#define TERM_FG_BLACK() printf("\e[30m")
#define TERM_FGBG_NORMAL() printf("\e[107m\e[30m")
#define TERM_FGBG_HIGHLIGHT() printf("\e[48;5;228m\e[38;5;124m")

struct subpattern_instance {
	struct timespec ts;
	char taskname[16];
	pid_t task;
	enum subpattern_boundary bound;
	struct subpattern_instance *partner;
	struct subpattern_definition *def;
	void *data;

	int is_significant;
	int level;
	unsigned long tracelineno;

	TAILQ_ENTRY(subpattern_instance) list_trace;
	LIST_ENTRY(subpattern_instance) list_open;
};

static LIST_HEAD(listhead_definitions, subpattern_definition) head_def;
static TAILQ_HEAD(listhead_instances, subpattern_instance) head_inst;
static LIST_HEAD(listhead_open, subpattern_instance) head_open;
static int def_id_last;

static unsigned long tracelineno;

int register_subpattern(struct subpattern_definition *def)
{
	def_id_last++;
	def->id = def_id_last;
	LIST_INSERT_HEAD(&head_def, def, list);

	return 0;
}

static void check_match(const char *line, struct subpattern_instance *inbound,
			struct timespec *ts, pid_t task, const char *taskname)
{
	struct subpattern_definition *sp_def;
	struct subpattern_instance *sp_inst;
	enum subpattern_boundary bound = in;
	void *inbound_data = NULL;
	void *data;

	/*
	 * If we already have an inbound instance, we are
	 * looking for the outbound instance.
	 */
	if (inbound) {
		bound = out;
		inbound_data = inbound->data;
	}

	LIST_FOREACH(sp_def, &head_def, list) {

		if (!sp_def->ops || !sp_def->ops->match)
			continue;

		if (inbound && inbound->def->id != sp_def->id)
			continue;

		data = sp_def->ops->match(line, task, bound, inbound_data);
		if (!data)
			continue;

		sp_inst = calloc(1, sizeof(*sp_inst));
		if (!sp_inst) {
			fprintf(stderr, "calloc failed: %s\n",
				strerror(errno));
			continue;
		}

		sp_inst->ts.tv_sec = ts->tv_sec;
		sp_inst->ts.tv_nsec = ts->tv_nsec;
		sp_inst->bound = bound;
		sp_inst->def = sp_def;
		sp_inst->task = task;
		strcpy(sp_inst->taskname, taskname);
		sp_inst->data = data;
		sp_inst->tracelineno = tracelineno;

		TAILQ_INSERT_TAIL(&head_inst, sp_inst, list_trace);

		if (inbound) {
			sp_inst->partner = inbound;
			inbound->partner = sp_inst;

			/*
			 * There can only be one pair. Since we've found
			 * a partner, we must stop looking. The caller
			 * is responsible for noticing the open instance
			 * now has a partner and therefore must be
			 * removed from the open list.
			 */
			break;
		} else {
			LIST_INSERT_HEAD(&head_open, sp_inst, list_open);
		}
	}
}

static int deepest_level;
static char levels[32];

static void print_blankline(struct timespec *ts, int so_level)
{
	char tsbuf[16];
	int i;

	snprintf(tsbuf, sizeof(tsbuf), "%lu", ts->tv_sec);
	for (i = strlen(tsbuf); i > 0; i--)
		printf(" ");
	printf("        ");

	for (i = 1; i < deepest_level; i++) {
		if (i == so_level)
			TERM_FGBG_HIGHLIGHT();

		if (levels[i])
			printf("|  ");
		else
			printf("   ");

		if (i == so_level)
			TERM_FG_BLACK();
	}
}

static void print_instance(struct subpattern_instance *sp_inst, int so_level)
{
	int first = 1;
	int i;

	if (sp_inst->def->ops->print) {
		printf("%lu.%06lu ", sp_inst->ts.tv_sec,
		       sp_inst->ts.tv_nsec / 1000);
		for (i = 1; i < sp_inst->level; i++) {
			if (i == so_level)
				TERM_FGBG_HIGHLIGHT();

			if (levels[i])
				printf("|  ");
			else
				printf("   ");

			if (i == so_level)
				TERM_FG_BLACK();
		}

		for (; i < deepest_level; i++) {
			if (i == so_level)
				TERM_FGBG_HIGHLIGHT();

			if (first) {
				first = 0;
				if (sp_inst->bound == in)
					printf(",--");
				else
					printf("`--");
			} else {
				if (levels[i])
					printf("+--");
				else
					printf("---");
			}
		}
		printf(" ");
		sp_inst->def->ops->print(sp_inst->data);
		printf(" (%s-%u)", sp_inst->taskname, sp_inst->task);
	}
}

static char schedin_pidstr[32];
static char schedout_pidstr[32];

int subpattern_handle_traceline(const char *traceline)
{
	struct subpattern_instance *sp_inst;
	struct timespec ts;
	char taskname[16];
	pid_t task;
	char *p;

	tracelineno++;

	/* parse task */
	p = strstr(traceline, " [");
	if (!p)
		return -1;
	while (*p != '-') {
		if (p == traceline)
			return -1;
		p--;
	}
	task = strtoul(p + 1, NULL, 10);
	while (*traceline == ' ')
		traceline++;
	snprintf(taskname, sizeof(taskname), "%s", traceline);
	p = strchr(taskname, '-');
	if (p)
		*p = 0;

	/* parse timestamp */
	p = strstr(traceline, ": ");
	if (!p)
		return -1;
	while (*p != '.') {
		if (p == traceline)
			return -1;
		p--;
	}
	ts.tv_nsec = strtoul(p + 1, NULL, 10) * 1000;
	while (*p != ' ') {
		if (p == traceline)
			return -1;
		p--;
	}
	ts.tv_sec = strtoul(p + 1, NULL, 10);

	/* check for outbound on line */
	LIST_FOREACH(sp_inst, &head_open, list_open) {
		check_match(traceline, sp_inst, &ts, task, taskname);
		if (sp_inst->partner)
			LIST_REMOVE(sp_inst, list_open);
	}

	/* check for new inbound(s) on line */
	check_match(traceline, NULL, &ts, task, taskname);

	return 0;
}

static pid_t focus_task;

void subpattern_init(const char *tracingpath, pid_t task)
{
	struct subpattern_definition *sp_def;

	LIST_INIT(&head_def);
	TAILQ_INIT(&head_inst);
	LIST_INIT(&head_open);

	register_sched_out_nonint_sleeping();
	register_sched_out_sleeping();
	register_sched_out_runnable();
	register_sched_latency();
	register_prio_boost();
	register_syscall();

	LIST_FOREACH(sp_def, &head_def, list) {
		if (!sp_def->ops->enable)
			continue;
		sp_def->ops->enable(tracingpath, task);
	}

	snprintf(schedin_pidstr, sizeof(schedin_pidstr), " next_pid=%d ",
		 task);
	snprintf(schedout_pidstr, sizeof(schedout_pidstr), " prev_pid=%d ",
		 task);
	focus_task = task;
}

static int is_sp_ts_lt(struct subpattern_instance *lhs,
		       struct subpattern_instance *rhs)
{
	if (lhs->ts.tv_sec == rhs->ts.tv_sec) {
		if (lhs->ts.tv_nsec < rhs->ts.tv_nsec)
			return 1;

	} else if (lhs->ts.tv_sec < rhs->ts.tv_sec) {
		return 1;
	}

	return 0;
}

static int is_sp_ts_gt(struct subpattern_instance *lhs,
		       struct subpattern_instance *rhs)
{
	if (lhs->ts.tv_sec == rhs->ts.tv_sec) {
		if (lhs->ts.tv_nsec > rhs->ts.tv_nsec)
			return 1;

	} else if (lhs->ts.tv_sec > rhs->ts.tv_sec) {
		return 1;
	}

	return 0;
}

static void mark_sp_significant(struct subpattern_instance *sp_inst);

static void range_identify_significant(struct subpattern_instance *begin,
				       struct subpattern_instance *end)
{
	struct subpattern_instance *sp_inst;

	for (sp_inst = TAILQ_NEXT(begin, list_trace);
	     sp_inst != end;
	     sp_inst = TAILQ_NEXT(sp_inst, list_trace)) {

		/*
		 * We are only interested if the partner bound is
		 * outside of the current subpattern (begin/end). We
		 * only need to check the partner because obviously
		 * "sp_inst" is within the current subpattern.
		 */

		if (!sp_inst->partner)
			continue;

		if (is_sp_ts_gt(sp_inst->partner, begin) &&
		    is_sp_ts_lt(sp_inst->partner, end)) {
			continue;
		}

		mark_sp_significant(sp_inst);
	}
}

static int contains_significant(struct subpattern_instance *sp_inst)
{
	struct subpattern_instance *begin;
	struct subpattern_instance *end;

	/* ignore open subpatterns */
	if (!sp_inst->partner)
		return 0;

	/* have we already been marked? */
	if (sp_inst->is_significant)
		return 0;

	begin = sp_inst;
	end = sp_inst->partner;

	for (sp_inst = TAILQ_NEXT(begin, list_trace);
	     sp_inst != end;
	     sp_inst = TAILQ_NEXT(sp_inst, list_trace)) {

		if (sp_inst->is_significant)
			return 1;
	}

	return 0;
}

static void mark_sp_significant(struct subpattern_instance *sp_inst)
{
	struct subpattern_instance *begin;
	struct subpattern_instance *end;

	/* ignore open subpatterns */
	if (!sp_inst->partner)
		return;

	/* have we already been marked? */
	if (sp_inst->is_significant)
		return;

	if (sp_inst->bound == in) {
		begin = sp_inst;
		end = sp_inst->partner;
	} else {
		begin = sp_inst->partner;
		end = sp_inst;
	}

	/* is the subpattern (at least partially) relevant? */
	if ((begin->def->ops->is_relevant &&
	     !begin->def->ops->is_relevant(focus_task, begin->data)) &&
	    (end->def->ops->is_relevant &&
	     !end->def->ops->is_relevant(focus_task, end->data))) {
		return;
	}

	/* we are significant! */
	begin->is_significant = 1;
	end->is_significant = 1;

	/*
	 * Now that we have a new significant subpattern, we must check for
	 * new significant subpatterns. These are subpatterns that touch
	 * or cross the boundaries of this subpattern.
	 *
	 * NOTE: This does not detect subpatterns that begin before and
	 *       end after the range. That must be detected elsewhere.
	 */
	range_identify_significant(begin, end);
}

void subpattern_cleanup(void)
{
	struct subpattern_instance *last_inst = NULL;
	struct subpattern_definition *sp_def;
	struct subpattern_instance *sp_inst;
	int next_level = 1;
	int so_level = 0;
	int ret;

	memset(levels, 0, sizeof(levels));
	levels[0] = 255;

	/*
	 * First we indentify significant subpatterns based on the
	 * overlapping of significant subpatterns. (A subpattern
	 * begins XOR ends within a significant subpattern.)
	 */
	TAILQ_FOREACH(sp_inst, &head_inst, list_trace) {
		if (!sp_inst->def->has_sched_switch)
			continue;

		mark_sp_significant(sp_inst);
	}

	/*
	 * Second we identify significant subpatterns based on containment
	 * of significant subpatterns. (A subpattern begins before and
	 * ends after a significant subpattern.)
	 */
	TAILQ_FOREACH(sp_inst, &head_inst, list_trace) {
		if (sp_inst->is_significant)
			continue;

		if (sp_inst->bound != in)
			continue;

		if (sp_inst->def->ops->is_relevant &&
		    !sp_inst->def->ops->is_relevant(focus_task,
						    sp_inst->data)) {
			continue;
		}

		if (contains_significant(sp_inst))
			mark_sp_significant(sp_inst);
	}

	/*
	 * Identify the print levels for the subpatterns for
	 * a pretty output.
	 */
	TAILQ_FOREACH(sp_inst, &head_inst, list_trace) {
		if (!sp_inst->is_significant)
			continue;

		if (sp_inst->bound == in) {
			levels[next_level] = 1;
			sp_inst->level = next_level;
			next_level++;
			if (next_level > deepest_level)
				deepest_level = next_level;
			continue;
		}

		sp_inst->level = sp_inst->partner->level;
		levels[sp_inst->level] = 0;
		if (next_level - 1 > sp_inst->level)
			continue;

		while (levels[next_level - 1] == 0)
			next_level--;
	}

	/*
	 * All significant subpatterns have been marked.
	 * Print them.
	 */
	TAILQ_FOREACH(sp_inst, &head_inst, list_trace) {
		if (!sp_inst->is_significant)
			continue;

		if (last_inst &&
		    sp_inst->tracelineno != last_inst->tracelineno) {
			TERM_FGBG_NORMAL();

			print_blankline(&sp_inst->ts, so_level);

			TERM_CURSOR_END();
			printf("\n");
		}

		if (sp_inst->bound == in)
			levels[sp_inst->level] = 1;
		else
			levels[sp_inst->level] = 0;

		if (sp_inst->def->ops->sched_out) {
			ret = sp_inst->def->ops->sched_out(focus_task,
							   sp_inst->data);
			if (ret > 0)
				so_level = sp_inst->level;
		} else {
			ret = 0;
		}

		TERM_FGBG_NORMAL();

		print_instance(sp_inst, so_level);

		TERM_CURSOR_END();
		printf("\n");

		if (ret < 0)
			so_level = 0;

		last_inst = sp_inst;
	}
	TERM_RESET();
	TERM_CURSOR_END();
	printf("\n");

	while (LIST_FIRST(&head_open))
		LIST_REMOVE(LIST_FIRST(&head_open), list_open);

	for (sp_inst = TAILQ_FIRST(&head_inst); sp_inst;
	     sp_inst = TAILQ_FIRST(&head_inst)) {

		TAILQ_REMOVE(&head_inst, sp_inst, list_trace);
		if (sp_inst->def->ops->free_data)
			sp_inst->def->ops->free_data(sp_inst->data);
		free(sp_inst);
	}

	for (sp_def = LIST_FIRST(&head_def); sp_def;
	     sp_def = LIST_FIRST(&head_def)) {

		LIST_REMOVE(sp_def, list);
		if (sp_def->ops->unregister)
			sp_def->ops->unregister(sp_def);
	}
}
