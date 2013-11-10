/*
 * Type declarations for netinfo.
 *
 * Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 */
#ifndef __WICKED_TYPES_H__
#define __WICKED_TYPES_H__

#include <dborb/constants.h>
#include <stdint.h>

typedef unsigned char		ni_bool_t;
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

typedef union ni_sockaddr	ni_sockaddr_t;
typedef struct ni_netconfig	ni_netconfig_t;

typedef struct ni_dbus_server	ni_dbus_server_t;
typedef struct ni_dbus_client	ni_dbus_client_t;

typedef struct ni_socket	ni_socket_t;
typedef struct ni_buffer	ni_buffer_t;
typedef struct ni_buffer_chain	ni_buffer_chain_t;
typedef struct ni_extension	ni_extension_t;
typedef struct ni_script_action	ni_script_action_t;

typedef struct ni_shellcmd	ni_shellcmd_t;
typedef struct ni_process	ni_process_t;
typedef struct ni_process_exit_info  ni_process_exit_info_t;

/*
 * These are used by the XML and XPATH code.
 */
typedef struct xpath_format xpath_format_t;
typedef struct xpath_enode xpath_enode_t;
typedef struct xml_document xml_document_t;
typedef struct xml_node xml_node_t;
typedef struct ni_xs_type	ni_xs_type_t;
typedef struct ni_xs_scope	ni_xs_scope_t;
typedef struct ni_xs_method	ni_xs_method_t;
typedef struct ni_xs_service	ni_xs_service_t;

typedef struct xpath_format_array {
	unsigned int		count;
	xpath_format_t **	data;
} xpath_format_array_t;

typedef union ni_uuid {
	unsigned char		octets[16];
	uint32_t		words[4];
} ni_uuid_t;
#define NI_UUID_INIT		{ .words = { 0, 0, 0, 0 } }

/*
 * Range of unsigned values
 */
typedef struct ni_uint_range {
	unsigned int		min, max;
} ni_uint_range_t;

static inline void
ni_uint_range_update_min(ni_uint_range_t *r, unsigned int min)
{
	if (min > r->min)
		r->min = min;
}

static inline void
ni_uint_range_update_max(ni_uint_range_t *r, unsigned int max)
{
	if (max < r->max)
		r->max = max;
}

/*
 * Range of signed values
 */
typedef struct ni_int_range {
	int			min, max;
} ni_int_range_t;

/*
 * offsetof/container_of macros
 */
#define ni_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define ni_container_of(ptr, TYPE, MEMBER) ({            \
		 const typeof( ((TYPE *)0)->MEMBER ) *__mptr = (ptr);    \
		 (TYPE *)( (char *)__mptr - ni_offsetof(TYPE,MEMBER) );})


#endif /* __WICKED_TYPES_H__ */
