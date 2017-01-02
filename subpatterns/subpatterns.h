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

#ifndef SUBPATTERNS_H
#define SUBPATTERNS_H

extern int register_sched_out_nonint_sleeping(void);
extern int register_sched_out_sleeping(void);
extern int register_sched_out_runnable(void);
extern int register_sched_latency(void);
extern int register_prio_boost(void);
extern int register_syscall(void);

#endif /* SUBPATTERNS_H */
