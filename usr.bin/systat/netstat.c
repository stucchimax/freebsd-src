/*-
 * Copyright (c) 1980, 1992, 1993
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

__FBSDID("$FreeBSD: src/usr.bin/systat/netstat.c,v 1.20 2003/08/01 20:28:20 dwmalone Exp $");

#ifdef lint
static const char sccsid[] = "@(#)netstat.c	8.1 (Berkeley) 6/6/93";
#endif

/*
 * netstat
 */
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/in_pcb.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_var.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <netdb.h>
#include <nlist.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>

#include "systat.h"
#include "extern.h"

static struct netinfo *enter(struct inpcb *, int, const char *);
static void enter_kvm(struct inpcb *, struct socket *, int, const char *);
static void enter_sysctl(struct inpcb *, struct xsocket *, int, const char *);
static void fetchnetstat_kvm(void);
static void fetchnetstat_sysctl(void);
static char *inetname(struct in_addr);
static void inetprint(struct in_addr *, int, const char *);

#define	streq(a,b)	(strcmp(a,b)==0)
#define	YMAX(w)		((w)->_maxy-1)

WINDOW *
opennetstat()
{
	sethostent(1);
	setnetent(1);
	return (subwin(stdscr, LINES-5-1, 0, 5, 0));
}

struct netinfo {
	TAILQ_ENTRY(netinfo) chain;
	short	ni_line;		/* line on screen */
	short	ni_seen;		/* 0 when not present in list */
	short	ni_flags;
#define	NIF_LACHG	0x1		/* local address changed */
#define	NIF_FACHG	0x2		/* foreign address changed */
	short	ni_state;		/* tcp state */
	const char	*ni_proto;		/* protocol */
	struct	in_addr ni_laddr;	/* local address */
	long	ni_lport;		/* local port */
	struct	in_addr	ni_faddr;	/* foreign address */
	long	ni_fport;		/* foreign port */
	u_int	ni_rcvcc;		/* rcv buffer character count */
	u_int	ni_sndcc;		/* snd buffer character count */
};

TAILQ_HEAD(netinfohead, netinfo) netcb = TAILQ_HEAD_INITIALIZER(netcb);

static	int aflag = 0;
static	int nflag = 0;
static	int lastrow = 1;

void
closenetstat(w)
        WINDOW *w;
{
	struct netinfo *p;

	endhostent();
	endnetent();
	TAILQ_FOREACH(p, &netcb, chain) {
		if (p->ni_line != -1)
			lastrow--;
		p->ni_line = -1;
	}
        if (w != NULL) {
		wclear(w);
		wrefresh(w);
		delwin(w);
	}
}

static const char *miblist[] = {
	"net.inet.tcp.pcblist",
	"net.inet.udp.pcblist" 
};

struct nlist namelist[] = {
#define	X_TCB	0
	{ "tcb" },
#define	X_UDB	1
	{ "udb" },
	{ "" },
};

int
initnetstat()
{
	protos = TCP|UDP;
	return(1);
}

void
fetchnetstat()
{
	if (use_kvm)
		fetchnetstat_kvm();
	else
		fetchnetstat_sysctl();
}

static void
fetchnetstat_kvm()
{
	struct inpcb *next;
	struct netinfo *p;
	struct inpcbhead head;
	struct inpcb inpcb;
	struct socket sockb;
	struct tcpcb tcpcb;
	void *off;
	int istcp;

	if (namelist[X_TCB].n_value == 0)
		return;
	TAILQ_FOREACH(p, &netcb, chain)
		p->ni_seen = 0;
	if (protos&TCP) {
		off = NPTR(X_TCB);
		istcp = 1;
	}
	else if (protos&UDP) {
		off = NPTR(X_UDB);
		istcp = 0;
	}
	else {
		error("No protocols to display");
		return;
	}
again:
	KREAD(off, &head, sizeof (struct inpcbhead));
	LIST_FOREACH(next, &head, inp_list) {
		KREAD(next, &inpcb, sizeof (inpcb));
		next = &inpcb;
		if (!aflag && inet_lnaof(inpcb.inp_laddr) == INADDR_ANY)
			continue;
		if (nhosts && !checkhost(&inpcb))
			continue;
		if (nports && !checkport(&inpcb))
			continue;
		KREAD(inpcb.inp_socket, &sockb, sizeof (sockb));
		if (istcp) {
			KREAD(inpcb.inp_ppcb, &tcpcb, sizeof (tcpcb));
			enter_kvm(&inpcb, &sockb, tcpcb.t_state, "tcp");
		} else
			enter_kvm(&inpcb, &sockb, 0, "udp");
	}
	if (istcp && (protos&UDP)) {
		istcp = 0;
		off = NPTR(X_UDB);
		goto again;
	}
}

static void
fetchnetstat_sysctl()
{
	struct netinfo *p;
	int idx;
	struct xinpgen *inpg;
	char *cur, *end;
	struct inpcb *inpcb;
	struct xinpcb *xip;
	struct xtcpcb *xtp;
	int plen;
	size_t lsz;

	TAILQ_FOREACH(p, &netcb, chain)
		p->ni_seen = 0;
	if (protos&TCP) {
		idx = 0;
	} else if (protos&UDP) {
		idx = 1;
	} else {
		error("No protocols to display");
		return;
	}

	for (;idx < 2; idx++) {
		if (idx == 1 && !(protos&UDP))
			break;
		inpg = (struct xinpgen *)sysctl_dynread(miblist[idx], &lsz);
		if (inpg == NULL) {
			error("sysctl(%s...) failed", miblist[idx]);
			continue;
		}
		/* 
		 * We currently do no require a consistent pcb list.
		 * Try to be robust in case of struct size changes 
		 */
		cur = ((char *)inpg) + inpg->xig_len;
		/* There is also a trailing struct xinpgen */
		end = ((char *)inpg) + lsz - inpg->xig_len;
		if (end <= cur) {
			free(inpg);
			continue;
		}
		if (idx == 0) { /* TCP */
			xtp = (struct xtcpcb *)cur;
			plen = xtp->xt_len;
		} else {
			xip = (struct xinpcb *)cur;
			plen = xip->xi_len;
		}
		while (cur + plen <= end) {
			if (idx == 0) { /* TCP */
				xtp = (struct xtcpcb *)cur;
				inpcb = &xtp->xt_inp;
			} else {
				xip = (struct xinpcb *)cur;
				inpcb = &xip->xi_inp;
			}
			cur += plen;

			if (!aflag && inet_lnaof(inpcb->inp_laddr) == 
			    INADDR_ANY)
				continue;
			if (nhosts && !checkhost(inpcb))
				continue;
			if (nports && !checkport(inpcb))
				continue;
			if (idx == 0)	/* TCP */
				enter_sysctl(inpcb, &xtp->xt_socket, 
				    xtp->xt_tp.t_state, "tcp");
			else		/* UDP */
				enter_sysctl(inpcb, &xip->xi_socket, 0, "udp");
		}
		free(inpg);
	}
}

static void
enter_kvm(inp, so, state, proto)
	struct inpcb *inp;
	struct socket *so;
	int state;
	const char *proto;
{
	struct netinfo *p;

	if ((p = enter(inp, state, proto)) != NULL) {
		p->ni_rcvcc = so->so_rcv.sb_cc;
		p->ni_sndcc = so->so_snd.sb_cc;
	}
}

static void
enter_sysctl(inp, so, state, proto)
	struct inpcb *inp;
	struct xsocket *so;
	int state;
	const char *proto;
{
	struct netinfo *p;

	if ((p = enter(inp, state, proto)) != NULL) {
		p->ni_rcvcc = so->so_rcv.sb_cc;
		p->ni_sndcc = so->so_snd.sb_cc;
	}
}


static struct netinfo *
enter(inp, state, proto)
	struct inpcb *inp;
	int state;
	const char *proto;
{
	struct netinfo *p;

	/*
	 * Only take exact matches, any sockets with
	 * previously unbound addresses will be deleted
	 * below in the display routine because they
	 * will appear as ``not seen'' in the kernel
	 * data structures.
	 */
	TAILQ_FOREACH(p, &netcb, chain) {
		if (!streq(proto, p->ni_proto))
			continue;
		if (p->ni_lport != inp->inp_lport ||
		    p->ni_laddr.s_addr != inp->inp_laddr.s_addr)
			continue;
		if (p->ni_faddr.s_addr == inp->inp_faddr.s_addr &&
		    p->ni_fport == inp->inp_fport)
			break;
	}
	if (p == NULL) {
		if ((p = malloc(sizeof(*p))) == NULL) {
			error("Out of memory");
			return NULL;
		}
		TAILQ_INSERT_HEAD(&netcb, p, chain);
		p->ni_line = -1;
		p->ni_laddr = inp->inp_laddr;
		p->ni_lport = inp->inp_lport;
		p->ni_faddr = inp->inp_faddr;
		p->ni_fport = inp->inp_fport;
		p->ni_proto = strdup(proto);
		p->ni_flags = NIF_LACHG|NIF_FACHG;
	}
	p->ni_state = state;
	p->ni_seen = 1;
	return p;
}

/* column locations */
#define	LADDR	0
#define	FADDR	LADDR+23
#define	PROTO	FADDR+23
#define	RCVCC	PROTO+6
#define	SNDCC	RCVCC+7
#define	STATE	SNDCC+7


void
labelnetstat()
{
	if (use_kvm && namelist[X_TCB].n_type == 0)
		return;
	wmove(wnd, 0, 0); wclrtobot(wnd);
	mvwaddstr(wnd, 0, LADDR, "Local Address");
	mvwaddstr(wnd, 0, FADDR, "Foreign Address");
	mvwaddstr(wnd, 0, PROTO, "Proto");
	mvwaddstr(wnd, 0, RCVCC, "Recv-Q");
	mvwaddstr(wnd, 0, SNDCC, "Send-Q");
	mvwaddstr(wnd, 0, STATE, "(state)");
}

void
shownetstat()
{
	struct netinfo *p, *q;

	/*
	 * First, delete any connections that have gone
	 * away and adjust the position of connections
	 * below to reflect the deleted line.
	 */
	p = TAILQ_FIRST(&netcb);
	while (p != NULL) {
		if (p->ni_line == -1 || p->ni_seen) {
			p = TAILQ_NEXT(p, chain);
			continue;
		}
		wmove(wnd, p->ni_line, 0); wdeleteln(wnd);
		TAILQ_FOREACH(q, &netcb, chain)
			if (q != p && q->ni_line > p->ni_line) {
				q->ni_line--;
				/* this shouldn't be necessary */
				q->ni_flags |= NIF_LACHG|NIF_FACHG;
			}
		lastrow--;
		q = TAILQ_NEXT(p, chain);
		TAILQ_REMOVE(&netcb, p, chain);
		free(p);
		p = q;
	}
	/*
	 * Update existing connections and add new ones.
	 */
	TAILQ_FOREACH(p, &netcb, chain) {
		if (p->ni_line == -1) {
			/*
			 * Add a new entry if possible.
			 */
			if (lastrow > YMAX(wnd))
				continue;
			p->ni_line = lastrow++;
			p->ni_flags |= NIF_LACHG|NIF_FACHG;
		}
		if (p->ni_flags & NIF_LACHG) {
			wmove(wnd, p->ni_line, LADDR);
			inetprint(&p->ni_laddr, p->ni_lport, p->ni_proto);
			p->ni_flags &= ~NIF_LACHG;
		}
		if (p->ni_flags & NIF_FACHG) {
			wmove(wnd, p->ni_line, FADDR);
			inetprint(&p->ni_faddr, p->ni_fport, p->ni_proto);
			p->ni_flags &= ~NIF_FACHG;
		}
		mvwaddstr(wnd, p->ni_line, PROTO, p->ni_proto);
		mvwprintw(wnd, p->ni_line, RCVCC, "%6u", p->ni_rcvcc);
		mvwprintw(wnd, p->ni_line, SNDCC, "%6u", p->ni_sndcc);
		if (streq(p->ni_proto, "tcp")) {
			if (p->ni_state < 0 || p->ni_state >= TCP_NSTATES)
				mvwprintw(wnd, p->ni_line, STATE, "%d",
				    p->ni_state);
			else
				mvwaddstr(wnd, p->ni_line, STATE,
				    tcpstates[p->ni_state]);
		}
		wclrtoeol(wnd);
	}
	if (lastrow < YMAX(wnd)) {
		wmove(wnd, lastrow, 0); wclrtobot(wnd);
		wmove(wnd, YMAX(wnd), 0); wdeleteln(wnd);	/* XXX */
	}
}

/*
 * Pretty print an Internet address (net address + port).
 * If the nflag was specified, use numbers instead of names.
 */
static void
inetprint(in, port, proto)
	struct in_addr *in;
	int port;
	const char *proto;
{
	struct servent *sp = 0;
	char line[80], *cp;

	snprintf(line, sizeof(line), "%.*s.", 16, inetname(*in));
	cp = index(line, '\0');
	if (!nflag && port)
		sp = getservbyport(port, proto);
	if (sp || port == 0)
		snprintf(cp, sizeof(line) - (cp - line), "%.8s", 
		    sp ? sp->s_name : "*");
	else
		snprintf(cp, sizeof(line) - (cp - line), "%d", 
		    ntohs((u_short)port));
	/* pad to full column to clear any garbage */
	cp = index(line, '\0');
	while (cp - line < 22)
		*cp++ = ' ';
	line[22] = '\0';
	waddstr(wnd, line);
}

/*
 * Construct an Internet address representation.
 * If the nflag has been supplied, give
 * numeric value, otherwise try for symbolic name.
 */
static char *
inetname(in)
	struct in_addr in;
{
	char *cp = 0;
	static char line[50];
	struct hostent *hp;
	struct netent *np;

	if (!nflag && in.s_addr != INADDR_ANY) {
		int net = inet_netof(in);
		int lna = inet_lnaof(in);

		if (lna == INADDR_ANY) {
			np = getnetbyaddr(net, AF_INET);
			if (np)
				cp = np->n_name;
		}
		if (cp == 0) {
			hp = gethostbyaddr((char *)&in, sizeof (in), AF_INET);
			if (hp)
				cp = hp->h_name;
		}
	}
	if (in.s_addr == INADDR_ANY)
		strcpy(line, "*");
	else if (cp)
		snprintf(line, sizeof(line), "%s", cp);
	else {
		in.s_addr = ntohl(in.s_addr);
#define C(x)	((x) & 0xff)
		snprintf(line, sizeof(line), "%u.%u.%u.%u", C(in.s_addr >> 24),
			C(in.s_addr >> 16), C(in.s_addr >> 8), C(in.s_addr));
	}
	return (line);
}

int
cmdnetstat(cmd, args)
	const char *cmd, *args;
{
	if (prefix(cmd, "all")) {
		aflag = !aflag;
		goto fixup;
	}
	if  (prefix(cmd, "numbers") || prefix(cmd, "names")) {
		struct netinfo *p;
		int new;

		new = prefix(cmd, "numbers");
		if (new == nflag)
			return (1);
		TAILQ_FOREACH(p, &netcb, chain) {
			if (p->ni_line == -1)
				continue;
			p->ni_flags |= NIF_LACHG|NIF_FACHG;
		}
		nflag = new;
		goto redisplay;
	}
	if (!netcmd(cmd, args))
		return (0);
fixup:
	fetchnetstat();
redisplay:
	shownetstat();
	refresh();
	return (1);
}
