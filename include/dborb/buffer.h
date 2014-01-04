/*
 * Types and functions for encapsulating buffers.
 * Needed by the DHCP and ARP code.
 *
 * Copyright (C) 2010-2014, Olaf Kirch <okir@suse.de>
 */


#ifndef __WICKED_DHCP_BUFFER_H__
#define __WICKED_DHCP_BUFFER_H__

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dborb/types.h>
#include <dborb/util.h>

struct ni_buffer {
	unsigned char *		base;
	size_t			head;
	size_t			tail;
	size_t			size;
	unsigned int		overflow : 1,
				underflow : 1,
				allocated : 1;
};

struct ni_buffer_chain {
	ni_buffer_chain_t *	next;
	ni_buffer_t *		data;
};

/* this should really be named init_writer */
static inline void
ni_buffer_init(ni_buffer_t *bp, void *base, size_t size)
{
	memset(bp, 0, sizeof(*bp));
	bp->base = (unsigned char *) base;
	bp->size = size;
}

static inline void
ni_buffer_init_dynamic(ni_buffer_t *bp, size_t size)
{
	memset(bp, 0, sizeof(*bp));
	bp->base = (unsigned char *) ni_calloc(1, size);
	bp->size = size;
	bp->allocated = 1;
}

static inline void
ni_buffer_destroy(ni_buffer_t *bp)
{
	if (bp->allocated)
		free(bp->base);
	memset(bp, 0, sizeof(*bp));
}

static inline ni_buffer_t *
ni_buffer_new_dynamic(size_t size)
{
	ni_buffer_t *bp;

	bp = ni_calloc(1, sizeof(*bp));
	ni_buffer_init_dynamic(bp, size);
	return bp;
}

static inline ni_buffer_t *
ni_buffer_new(size_t size)
{
	ni_buffer_t *bp;

	bp = ni_calloc(1, sizeof(*bp) + size);
	ni_buffer_init(bp, (char *) (bp + 1), size);
	return bp;
}

static inline void
ni_buffer_free(ni_buffer_t *bp)
{
	ni_buffer_destroy(bp);
	free(bp);
}

static inline void
ni_buffer_init_reader(ni_buffer_t *bp, void *base, size_t size)
{
	memset(bp, 0, sizeof(*bp));
	bp->base = (unsigned char *) base;
	bp->size = bp->tail = size;
}

static inline void
ni_buffer_clear(ni_buffer_t *bp)
{
	bp->head = bp->tail = 0;
}

static inline int
ni_buffer_put(ni_buffer_t *bp, const void *data, size_t len)
{
	if (bp->tail + len > bp->size) {
		bp->overflow = 1;
		return -1;
	}
	if (data)
		memcpy(bp->base + bp->tail, data, len);
	bp->tail += len;
	return 0;
}

static inline void
ni_buffer_putc(ni_buffer_t *bp, unsigned char cc)
{
	ni_buffer_put(bp, &cc, 1);
}

static inline void
ni_buffer_pad(ni_buffer_t *bp, size_t min_size, unsigned char padbyte)
{
	if (bp->tail >= min_size)
		return;
	memset(bp->base + bp->tail, padbyte, min_size - bp->tail);
	bp->tail = min_size;
}

static inline void *
ni_buffer_head(const ni_buffer_t *bp)
{
	return bp->base + bp->head;
}

static inline void *
ni_buffer_tail(const ni_buffer_t *bp)
{
	return bp->base + bp->tail;
}

static inline unsigned int
ni_buffer_count(const ni_buffer_t *bp)
{
	if (bp->tail > bp->head)
		return bp->tail - bp->head;
	else
		return 0;
}

static inline unsigned int
ni_buffer_tailroom(const ni_buffer_t *bp)
{
	if (bp->size > bp->tail)
		return bp->size - bp->tail;
	else
		return 0;
}

static inline int
ni_buffer_get(ni_buffer_t *bp, void *data, size_t len)
{
	if (bp->head + len > bp->tail) {
		bp->underflow = 1;
		return -1;
	}
	memcpy(data, bp->base + bp->head, len);
	bp->head += len;
	return 0;
}

static inline int
ni_buffer_getc(ni_buffer_t *bp)
{
	if (bp->head >= bp->tail)
		return EOF;
	return bp->base[bp->head++];
}

static inline int
ni_buffer_ungetc(ni_buffer_t *bp, char cc)
{
	if (bp->head == 0 || bp->base[bp->head - 1] != cc)
		return -1;
	bp->head--;
	return 0;
}

static inline int
ni_buffer_reserve_head(ni_buffer_t *bp, size_t headroom)
{
	if (bp->head != bp->tail)
		return -1;

	bp->head = bp->tail += headroom;
	return 0;
}

static inline void *
ni_buffer_push_head(ni_buffer_t *bp, size_t count)
{
	if (bp->head < count) {
		bp->overflow = 1;
		return NULL;
	}
	bp->head -= count;
	return bp->base + bp->head;
}

static inline void *
ni_buffer_push_tail(ni_buffer_t *bp, size_t count)
{
	void *result;

	if (bp->size - bp->tail < count) {
		bp->overflow = 1;
		return NULL;
	}

	result = bp->base + bp->tail;
	bp->tail += count;
	return result;
}

static inline void *
ni_buffer_pull_head(ni_buffer_t *bp, size_t count)
{
	void *result;

	if (bp->tail - bp->head < count) {
		bp->underflow = 1;
		return NULL;
	}

	result = bp->base + bp->head;
	bp->head += count;
	return result;
}

extern ni_bool_t	ni_buffer_ensure_tailroom(ni_buffer_t *, unsigned int);

extern void		ni_buffer_chain_discard(ni_buffer_chain_t **head);
extern void		ni_buffer_chain_append(ni_buffer_chain_t **head, ni_buffer_t *);
extern unsigned int	ni_buffer_chain_count(const ni_buffer_chain_t *);
extern ni_buffer_t *	ni_buffer_chain_get_next(ni_buffer_chain_t **head);

#endif /* __WICKED_DHCP_BUFFER_H__ */
