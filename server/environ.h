
#ifndef __TESTBUS_SERVER_ENVIRON_H__
#define __TESTBUS_SERVER_ENVIRON_H__

#include <dborb/util.h>
#include "types.h"

struct ni_testbus_env {
	ni_var_array_t		vars;
	ni_bool_t		sorted;
};

#define NI_TESTBUS_ENV_INIT		{ .vars = NI_VAR_ARRAY_INIT, .sorted = FALSE }

typedef struct ni_testbus_env_array ni_testbus_env_array_t;
struct ni_testbus_env_array {
	unsigned int		count;
	ni_testbus_env_t **	data;
};

#define NI_TESTBUS_ENV_ARRAY_INIT	{ .count = 0, .data = 0 }

extern ni_testbus_env_t *ni_testbus_global_env(void);
extern void		ni_testbus_env_init(ni_testbus_env_t *env);
extern void		ni_testbus_env_destroy(ni_testbus_env_t *);
extern ni_bool_t	ni_testbus_env_name_valid(const char *);
extern ni_bool_t	ni_testbus_env_name_reserved(const char *);

extern void		ni_testbus_setenv(ni_testbus_env_t *, const char *, const char *);
extern void		ni_testbus_unsetenv(ni_testbus_env_t *, const char *);
extern const char *	ni_testbus_getenv(ni_testbus_env_t *, const char *);
extern ni_bool_t	ni_testbus_env_merge(ni_testbus_env_t *result, ni_testbus_env_array_t *);

extern void		ni_testbus_env_array_init(ni_testbus_env_array_t *);
extern void		ni_testbus_env_array_destroy(ni_testbus_env_array_t *);
extern void		ni_testbus_env_array_append(ni_testbus_env_array_t *, ni_testbus_env_t *);

#endif /* __TESTBUS_SERVER_ENVIRON_H__ */

