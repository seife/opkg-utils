/*
 * update-alternatives.c
 * plain C port of the update-alternatives script from
 * git://git.yoctoproject.org/opkg-utils
 * because a shell script is just too slow.
 * (C) 2017 Stefan Seyfried <seife@tuxboxcvs.slipkontur.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

char *ad = NULL;
char *or = NULL;
char *altpath = NULL;

void usage(const char *msg)
{
	fprintf(stderr,
		"update-alternatives: %s\n"
		"Usage: update-alternatives --install <link> <name> <path> <priority>\n"
		"       update-alternatives --remove <name> <path>\n"
		"       update-alternatives --help\n"
		"       <link> is the link pointing to the provided path (ie. /usr/bin/foo).\n"
		"       <name> is the name in %s/alternatives (ie. foo)\n"
		"       <path> is the name referred to (ie. /usr/bin/foo-extra-spiffy)\n"
		"       <priority> is an integer; options with higher numbers are chosen.\n",
		msg, ad);
}

char *sanitize_path(char *path)
{
	int i;
	char *p = path;
	int len = strlen(path);
	/* remove all double slashes */
	for (i = 0; i < len; i++) {
		if (i < len-1 &&
		    path[i] == '/' && path[i+1] == '/')
			continue;
		*p++ = path[i];
	}
	*p = 0;
	/* p - path == strlen of changed path */
	if (or) {
		len = strlen(or);
		if (len &&
		    strncmp(path, or, len) == 0)
			memmove(path, path+len, p - path - len);
	}
	// fprintf(stderr, "%s: path '%s'\n", __func__, path);
	return path;
}

int mkdir_p(char *path, bool last)
{
	char *p = path;

	while (*++p) {
		if (*p != '/')
			continue;
		*p = 0;
		if (mkdir(path, 0777) &&
		    errno != EEXIST && errno != EISDIR) {
			fprintf(stderr, "%s: %s (%m)\n",__func__, path);
			*p = '/';
			return errno;
		}
		*p = '/';
	}
	if (! last)
		return 0;
	return mkdir(path, 0777);
}

int remove_alt(char *altfile, char *path)
{
	FILE *g;
	char *new;
	ssize_t rd;
	int plen = strlen(path);
	size_t n = 0;
	char *line = NULL;
	FILE *f = fopen(altfile, "r");
	if (!f)
		return 0;
	asprintf(&new, "%s.new", altfile);
	g = fopen(new, "w");
	if (!g) {
		fprintf(stderr, "%s: cannot open %s for writing (%m)\n", __func__, new);
		fclose(f);
		return 1;
	}
	while ((rd = getline(&line, &n, f)) > -1) {
		if (strncmp(line, path, plen) == 0 && rd > plen && line[plen] == ' ')
			continue;
		fwrite(line, 1, rd, g);
	}
	fclose(f);
	fclose(g);
	free(line);
	unlink(altfile);
	rename(new, altfile);
	return 0;
}

int find_best_alt(char *altfile)
{
	char *path = NULL, *p = NULL;
	char *link;
	char *line = NULL;
	size_t n = 0;
	ssize_t m;
	int pr = -1, pp;
	bool islink = false, exists = false;
	struct stat sb;
	FILE *f = fopen(altfile, "r");
	if (! f)
		return 0;

	m = getline(&line, &n, f);
	if (m == -1) {
		fprintf(stderr, "%s: getline failed (%m)\n", __func__);
		free(line);
		line = NULL;
		n = 0;
	} else {
		if (m > 1)
			m--;
		if (line[m] == '\n')
			line[m] = 0;
	}
	asprintf(&link, "%s%s", or, line ? line : "");
	while (getline(&line, &n, f) != -1) {
		if (sscanf(line, "%ms %d\n", &p, &pp) == 2) {
			if (pp > pr) {
				pr = pp;
				free(path);
				path = strdup(p);
			}
		}
		free(p);
		p = NULL;
	}
	free(line);
	fclose(f);
	if (! lstat(link, &sb))
		exists = true;
	if (exists && S_ISLNK(sb.st_mode))
		islink = true;
	if (pr < 0) { /* path has not been allocated! */
		printf("update-alternatives: removing %s as no more "
		       "alternatives exist for it\n", link);
		unlink(altfile);
		if (islink)
			unlink(link);
		return 0;
	}
	if (!exists || islink) {
		mkdir_p(link, false);
		unlink(link);
		printf("update-alternatives: Linking %s to %s\n", link, path);
		if (symlink(path, link))
			fprintf(stderr, "symlink %s -> %s failed: %m\n", link, path);
	} else {
		printf("update-alternatives: Error: not linking %s to %s "
		       "since %s exists and is not a link\n", link, path, link);
		return 1;
	}
	return 0;
}

int do_install(char **argv)
{
	struct stat sb;
	FILE *f;
	char *altfile;
	char *link = argv[2];
	char *name = argv[3];
	char *path = strdup(argv[4]);
	char *prio = argv[5];

	char *san = sanitize_path(path);
	/* register_alt $name $link */
	if (stat(ad, &sb) || !S_ISDIR(sb.st_mode))
		mkdir_p(ad, true);
	if (asprintf(&altfile, "%s/%s", ad, name) < 0) {
		fprintf(stderr, "%s: could not allocate string altfile\n", __func__);
		exit(3);
	}
	f = fopen(altfile, "r+");
	if (f) {
		char *line = NULL;
		size_t n = 0;
		ssize_t m = getline(&line, &n, f);
		if (m == -1) {
			fprintf(stderr, "%s: getline failed (%m)\n", __func__);
			free(line);
			line = strdup("");
		} else {
			if (m > 1)
				m--;
			if (line[m] == '\n')
				line[m] = 0;
		}
		fclose(f);
		if (strcmp(line, link) != 0) {
			fprintf(stderr, "update-alternatives: Error: cannot register alternative "
					"%s to %s since it is already registered to %s\n",
					name, link, line);
			free(line);
			// return 1;
		}
	} else {
		f = fopen(altfile, "w");
		if (f) {
			fprintf(f, "%s\n", link);
			fclose(f);
		} else
			fprintf(stderr, "%s: fopen(%s, w) failed (%m)\n", __func__, altfile);
	}
	
	/* add_alt $name $path $priority */
	remove_alt(altfile, san);
	f = fopen(altfile, "r+");
	if (f) {
		int pp;
		int pr = atoi(prio);
		int found = 0;
		char *line = NULL;
		size_t n = 0;
		while (getline(&line, &n, f) != -1) {
			if (sscanf(line, "%*s %d\n", &pp) != 1)
				continue;
			if (pp == pr)
				found++;
		}
		if (found)
			printf("Warn: update-alternatives: %s has multiple providers with "
			       "the same priority, please check %s for details\n", name, altfile);
		fprintf(f, "%s %s\n", san, prio);
		fclose(f);
		free(line);
	} else
		fprintf(stderr, "%s: could not open %s rw (%m)\n", __func__, altfile);

	/* find_best_alt $name */
	find_best_alt(altfile);
	free(altfile);
	free(path);
	return 0;
}

int do_remove(char **argv)
{
	char *altfile;
	char *name = argv[2];
	char *path = strdup(argv[3]);
	char *san = sanitize_path(path);

	if (asprintf(&altfile, "%s/%s", ad, name) < 0) {
		fprintf(stderr, "%s: could not allocate string altfile\n", __func__);
		exit(3);
	}
	remove_alt(altfile, san);
	find_best_alt(altfile);
	free(altfile);
	free(path);
	return 0;
}

int main(int argc, char **argv)
{
	int ret = 2;
	char *message = NULL;
	or = getenv("OPKG_OFFLINE_ROOT");
	if (or == NULL)
		or = "";
	asprintf(&ad, "%s%s", or, "/usr/lib/opkg/alternatives");

	if (argc < 2)
		usage("at least one of --install or --remove must appear");
	else if (!strcmp(argv[1], "--help")) {
		usage("help:");
		ret = 0;
	} else if (!strcmp(argv[1], "--install")) {
		if (argc < 6)
			usage("--install needs <link> <name> <path> <priority>");
		else
			ret =do_install(argv);
	} else if (!strcmp(argv[1], "--remove")) {
		if (argc < 4)
			usage("--remove needs <name> <path>");
		else
			ret = do_remove(argv);
	} else {
		asprintf(&message, "unknown argument `%s'", argv[1]);
		usage(message);
		free(message);
	}
	free(ad);
	return ret;
}

