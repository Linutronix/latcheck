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
#include <string.h>
#include <sys/param.h>

int set_tracing(const char *tracingpath, const char *attr_path,
		const char *attr_val)
{
	char path[MAXPATHLEN];
	FILE *f;
	int ret;

	ret = snprintf(path, sizeof(path), "%s/%s", tracingpath, attr_path);
	if (ret < 0 || (unsigned int)ret >= sizeof(path))
		return -1;

	f = fopen(path, "a");
	if (!f)
		return -1;

	ret = fwrite(attr_val, strlen(attr_val), 1, f);

	fclose(f);

	if (ret != 1)
		return -1;

	return 0;
}
