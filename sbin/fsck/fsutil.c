/*	$NetBSD: fsutil.c,v 1.7 1998/07/30 17:41:03 thorpej Exp $	*/

/*
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: fsutil.c,v 1.7 1998/07/30 17:41:03 thorpej Exp $");
#endif /* not lint */
__FBSDID("$FreeBSD: src/sbin/fsck/fsutil.c,v 1.5 2002/03/24 15:06:48 markm Exp $");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsutil.h"

static const char *dev = NULL;
static int hot = 0;
static int preen = 0;

static void vmsg(int, const char *, va_list) __printflike(2, 0);

void
setcdevname(const char *cd, int pr)
{
	dev = cd;
	preen = pr;
}

const char *
cdevname(void)
{
	return dev;
}

int
hotroot(void)
{
	return hot;
}

/*VARARGS*/
void
errexit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(8);
}

static void
vmsg(int fatal, const char *fmt, va_list ap)
{
	if (!fatal && preen)
		(void) printf("%s: ", dev);

	(void) vprintf(fmt, ap);

	if (fatal && preen)
		(void) printf("\n");

	if (fatal && preen) {
		(void) printf(
		    "%s: UNEXPECTED INCONSISTENCY; RUN %s MANUALLY.\n",
		    dev, getprogname());
		exit(8);
	}
}

/*VARARGS*/
void
pfatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(1, fmt, ap);
	va_end(ap);
}

/*VARARGS*/
void
pwarn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(0, fmt, ap);
	va_end(ap);
}

void
perror(const char *s)
{
	pfatal("%s (%s)", s, strerror(errno));
}

void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(1, fmt, ap);
	va_end(ap);
	exit(8);
}

const char *
unrawname(const char *name)
{
	static char unrawbuf[32];
	const char *dp;
	struct stat stb;

	if ((dp = strrchr(name, '/')) == 0)
		return (name);
	if (stat(name, &stb) < 0)
		return (name);
	if (!S_ISCHR(stb.st_mode))
		return (name);
	if (dp[1] != 'r')
		return (name);
	(void)snprintf(unrawbuf, 32, "%.*s/%s", (int)(dp - name), name, dp + 2);
	return (unrawbuf);
}

const char *
rawname(const char *name)
{
	static char rawbuf[32];
	const char *dp;

	if ((dp = strrchr(name, '/')) == 0)
		return (0);
	(void)snprintf(rawbuf, 32, "%.*s/r%s", (int)(dp - name), name, dp + 1);
	return (rawbuf);
}

const char *
devcheck(const char *origname)
{
	struct stat stslash, stchar;

	if (stat("/", &stslash) < 0) {
		perror("/");
		printf("Can't stat root\n");
		return (origname);
	}
	if (stat(origname, &stchar) < 0) {
		perror(origname);
		printf("Can't stat %s\n", origname);
		return (origname);
	}
	if (!S_ISCHR(stchar.st_mode)) {
		perror(origname);
		printf("%s is not a char device\n", origname);
	}
	return (origname);
}

/*
 * Get the mount point information for name.
 */
struct statfs *
getmntpt(const char *name)
{
	struct stat devstat, mntdevstat;
	char device[sizeof(_PATH_DEV) - 1 + MNAMELEN];
	char *devname;
	struct statfs *mntbuf, *statfsp;
	int i, mntsize, isdev;

	if (stat(name, &devstat) != 0)
		return (NULL);
	if (S_ISCHR(devstat.st_mode) || S_ISBLK(devstat.st_mode))
		isdev = 1;
	else
		isdev = 0;
	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (i = 0; i < mntsize; i++) {
		statfsp = &mntbuf[i];
		devname = statfsp->f_mntfromname;
		if (*devname != '/') {
			strcpy(device, _PATH_DEV);
			strcat(device, devname);
			strcpy(statfsp->f_mntfromname, device);
		}
		if (isdev == 0) {
			if (strcmp(name, statfsp->f_mntonname))
				continue;
			return (statfsp);
		}
		if (stat(devname, &mntdevstat) == 0 &&
		    mntdevstat.st_rdev == devstat.st_rdev)
			return (statfsp);
	}
	statfsp = NULL;
	return (statfsp);
}


void *
emalloc(size_t s)
{
	void *p;

	p = malloc(s);
	if (p == NULL)
		err(1, "malloc failed");
	return (p);
}


void *
erealloc(void *p, size_t s)
{
	void *q;

	q = realloc(p, s);
	if (q == NULL)
		err(1, "realloc failed");
	return (q);
}


char *
estrdup(const char *s)
{
	char *p;

	p = strdup(s);
	if (p == NULL)
		err(1, "strdup failed");
	return (p);
}
