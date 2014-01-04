/*
 * Buffer functions.
 * Most of these are inlines defined in buffer.h
 *
 * Copyright (C) 2010-2014 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dborb/buffer.h>
#include "util_priv.h"

ni_bool_t
ni_buffer_ensure_tailroom(ni_buffer_t *bp, unsigned int min_room)
{
	size_t	new_size;

	if (ni_buffer_tailroom(bp) >= min_room)
		return TRUE;

	new_size = bp->size + min_room;
	if (bp->allocated) {
		bp->base = xrealloc(bp->base, new_size);
	} else {
		unsigned char *new_base;

		new_base = xmalloc(new_size);
		memcpy(new_base, bp->base, bp->size);
		bp->base = new_base;
		bp->allocated = 1;
	}
	bp->size = new_size;
	return TRUE;
}

void
ni_buffer_chain_discard(ni_buffer_chain_t **head)
{
	ni_buffer_t *data;

	while ((data = ni_buffer_chain_get_next(head)) != NULL)
		ni_buffer_free(data);
}

void
ni_buffer_chain_append(ni_buffer_chain_t **head, ni_buffer_t *data)
{
	ni_buffer_chain_t *duckling, **pos;

	duckling = ni_calloc(1, sizeof(*duckling));
	duckling->data = data;

	for (pos = head; *pos; pos = &(*pos)->next)
		;
	*pos = duckling;
}

unsigned int
ni_buffer_chain_count(const ni_buffer_chain_t *chain)
{
	unsigned int count = 0;

	for (; chain; chain = chain->next)
		count += ni_buffer_count(chain->data);
	return count;
}

ni_buffer_t *
ni_buffer_chain_get_next(ni_buffer_chain_t **head)
{
	ni_buffer_chain_t *duckling;
	ni_buffer_t *data = NULL;

	if ((duckling = *head) != NULL) {
		*head = duckling->next;
		data = duckling->data;
		free(duckling);
	}
	return data;
}
