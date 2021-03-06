/*
 * Copyright (c) 1985, 1993
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

/*
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/*
 * Portions Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>
#define __OPTIMIZE__ 1
#include <libc-symbols.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ucresolv-internal.h>
#include <ucresolv_log.h>
#include <glibc-stdio.h>
#include <glibc-string.h>
#include <glibc-arpa/nameser.h>
#include <sys/time.h>

/* Options.  Leave them on. */
/* #define DEBUG */

#include <hp-timing.h>
#include <stdint.h>
#if HP_TIMING_AVAIL
# define RANDOM_BITS(Var) { uint64_t v64; HP_TIMING_NOW (v64); Var = v64; }
#endif
#define libresolv_hidden_def(name)
#ifndef __glibc_unlikely
#define __glibc_unlikely
#endif

typedef unsigned long __u_long;
typedef __u_long u_long;
typedef unsigned int __u_int;
typedef __u_int u_int;
typedef unsigned char __u_char;
typedef __u_char u_char;

/*
extern int __ns_name_compress(const char *src, u_char *dst, size_t dstsiz,
		 const u_char **dnptrs, const u_char **lastdnptr);

extern int gettimeofday (struct timeval *__tv, void *__tz); 
*/

#define NS_OPT_DNSSEC_OK        0x8000U
/*
 * Form all types of queries.
 * Returns the size of the result or -1.
 */
int
res_nmkquery(res_state statp,
	     int op,			/* opcode of query */
	     const char *dname,		/* domain name */
	     int class, int type,	/* class and type of query */
	     const u_char *data,	/* resource record data */
	     int datalen,		/* length of data */
	     const u_char *newrr_in,	/* new rr for modify or append */
	     u_char *buf,		/* buffer to put query */
	     int buflen)		/* size of buffer */
{
	HEADER *hp;
	u_char *cp;
	int n;
	u_char *dnptrs[20], **dpp, **lastdnptr;

	if (class < 0 || class > 65535
	    || type < 0 || type > 65535)
	  return -1;

#ifdef DEBUG
	if (statp->options & RES_DEBUG)
		ucresolv_info(";; res_nmkquery(%s, %s, %s, %s)\n",
		       _res_opcodes[op], dname, p_class(class), p_type(type));
#endif
	/*
	 * Initialize header fields.
	 */
	if ((buf == NULL) || (buflen < HFIXEDSZ))
		return (-1);
	memset(buf, 0, HFIXEDSZ);
	hp = (HEADER *) buf;
	/* We randomize the IDs every time.  The old code just
	   incremented by one after the initial randomization which
	   still predictable if the application does multiple
	   requests.  */
	int randombits;
	do
	  {
#ifdef RANDOM_BITS
	    RANDOM_BITS (randombits);
#else
	    struct timeval tv;
	    gettimeofday (&tv, NULL);
	    randombits = (tv.tv_sec << 8) ^ tv.tv_usec;
#endif
	  }
	while ((randombits & 0xffff) == 0);
	statp->id = (statp->id + randombits) & 0xffff;
	hp->id = statp->id;
	hp->opcode = op;
	hp->rd = (statp->options & RES_RECURSE) != 0;
	hp->rcode = NOERROR;
	cp = buf + HFIXEDSZ;
	buflen -= HFIXEDSZ;
	dpp = dnptrs;
	*dpp++ = buf;
	*dpp++ = NULL;
	lastdnptr = dnptrs + sizeof dnptrs / sizeof dnptrs[0];
	/*
	 * perform opcode specific processing
	 */
	switch (op) {
	case NS_NOTIFY_OP:
		if ((buflen -= QFIXEDSZ + (data == NULL ? 0 : RRFIXEDSZ)) < 0)
			return (-1);
		goto compose;

	case QUERY:
		if ((buflen -= QFIXEDSZ) < 0)
			return (-1);
	compose:
		n = ns_name_compress(dname, cp, buflen,
				     (const u_char **) dnptrs,
				     (const u_char **) lastdnptr);
		if (n < 0)
			return (-1);
		cp += n;
		buflen -= n;
		NS_PUT16 (type, cp);
		NS_PUT16 (class, cp);
		hp->qdcount = htons(1);
		if (op == QUERY || data == NULL)
			break;
		/*
		 * Make an additional record for completion domain.
		 */
		n = ns_name_compress((char *)data, cp, buflen,
				     (const u_char **) dnptrs,
				     (const u_char **) lastdnptr);
		if (__glibc_unlikely (n < 0))
			return (-1);
		cp += n;
		buflen -= n;
		NS_PUT16 (T_NULL, cp);
		NS_PUT16 (class, cp);
		NS_PUT32 (0, cp);
		NS_PUT16 (0, cp);
		hp->arcount = htons(1);
		break;

	default:
		return (-1);
	}
	return (cp - buf);
}
libresolv_hidden_def (res_nmkquery)


/* attach OPT pseudo-RR, as documented in RFC2671 (EDNS0). */
#ifndef T_OPT
#define T_OPT   41
#endif

int
__res_nopt(res_state statp,
	   int n0,                /* current offset in buffer */
	   u_char *buf,           /* buffer to put query */
	   int buflen,            /* size of buffer */
	   int anslen)            /* UDP answer buffer size */
{
	u_int16_t flags = 0;

#ifdef DEBUG
	if ((statp->options & RES_DEBUG) != 0U)
		ucresolv_info(";; res_nopt()\n");
#endif

	HEADER *hp = (HEADER *) buf;
	u_char *cp = buf + n0;
	u_char *ep = buf + buflen;

	if ((ep - cp) < 1 + RRFIXEDSZ)
		return -1;

	*cp++ = 0;	/* "." */

	NS_PUT16(T_OPT, cp);	/* TYPE */

	/* Lowering the advertised buffer size based on the actual
	   answer buffer size is desirable because the server will
	   minimize the reply to fit into the UDP packet (and A
	   non-minimal response might not fit the buffer).

	   The RESOLV_EDNS_BUFFER_SIZE limit could still result in TCP
	   fallback and a non-minimal response which has to be
	   hard-truncated in the stub resolver, but this is price to
	   pay for avoiding fragmentation.  (This issue does not
	   affect the nss_dns functions because they use the stub
	   resolver in such a way that it allocates a properly sized
	   response buffer.)  */
	{
	  uint16_t buffer_size;
	  if (anslen < 512)
	    buffer_size = 512;
	  else if (anslen > RESOLV_EDNS_BUFFER_SIZE)
	    buffer_size = RESOLV_EDNS_BUFFER_SIZE;
	  else
	    buffer_size = anslen;
	  NS_PUT16 (buffer_size, cp);
	}

	*cp++ = NOERROR;	/* extended RCODE */
	*cp++ = 0;		/* EDNS version */

	if (statp->options & RES_USE_DNSSEC) {
#ifdef DEBUG
		if (statp->options & RES_DEBUG)
			ucresolv_info(";; res_opt()... ENDS0 DNSSEC\n");
#endif
		flags |= NS_OPT_DNSSEC_OK;
	}

	NS_PUT16(flags, cp);
	NS_PUT16(0, cp);	/* RDLEN */
	hp->arcount = htons(ntohs(hp->arcount) + 1);

	return cp - buf;
}
