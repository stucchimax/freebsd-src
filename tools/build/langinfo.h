/* $FreeBSD: src/tools/build/langinfo.h,v 1.1 2003/04/05 20:30:30 imp Exp $ */

#ifndef LANGINFO_H
#define LANGINFO_H

#include <sys/cdefs.h>

#define YESEXPR 1

/* xargs only needs yesexpr, so that's all we implement, for english */
static inline const char *
nl_langinfo(int type __unused)
{
	return ("^[yY]");
}

#endif /* LANGINFO_H */
