/*
 * Private header file for netinfo library.
 * No user serviceable parts inside.
 *
 * Copyright (C) 2009-2012 Olaf Kirch <okir@suse.de>
 */

#ifndef __NETINFO_PRIV_H__
#define __NETINFO_PRIV_H__

#include <stdio.h>

#include <dborb/types.h>
#include <dborb/netinfo.h>
#include <dborb/logging.h>

typedef struct ni_capture	ni_capture_t;
typedef struct __ni_netlink	ni_netlink_t;

extern ni_netlink_t *		__ni_global_netlink;
extern int			__ni_global_iocfd;

/*
 * These constants describe why/how the interface has been brought up
 */
extern unsigned int	__ni_global_seqno;

extern ni_netlink_t *	__ni_netlink_open(int);
extern void		__ni_netlink_close(ni_netlink_t *);

/*
 * Allocation helpers
 */
extern void *		xcalloc(unsigned int, size_t);
extern char *		xstrdup(const char *);


#endif /* __NETINFO_PRIV_H__ */
