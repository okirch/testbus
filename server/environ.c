
#include <ctype.h>
#include "environ.h"
#include <dborb/util.h>
#include <dborb/logging.h>

static ni_testbus_env_t		__ni_testbus_global_env;

ni_testbus_env_t *
ni_testbus_global_env(void)
{
	static int initialized = 0;

	if (!initialized) {
		ni_testbus_env_init(&__ni_testbus_global_env);
		initialized = 1;
	}
	return &__ni_testbus_global_env;
}
void
ni_testbus_env_init(ni_testbus_env_t *env)
{
	memset(env, 0, sizeof(*env));
}

void
ni_testbus_env_destroy(ni_testbus_env_t *env)
{
	ni_var_array_destroy(&env->vars);
	memset(env, 0, sizeof(*env));
}

ni_bool_t
ni_testbus_env_name_valid(const char *name)
{
	if (*name != '_' && !isalpha(*name))
		return FALSE;

	while (*++name) {
		if (*name != '_' && !isalnum(*name))
			return FALSE;
	}
	return TRUE;
}

ni_bool_t
ni_testbus_env_name_reserved(const char *name)
{
	static const char reserved_prefix[] = "testbus_";

	if (!strncmp(name, reserved_prefix, sizeof(reserved_prefix)-1))
		return TRUE;

	return FALSE;
}

void
ni_testbus_setenv(ni_testbus_env_t *env, const char *name, const char *value)
{
	ni_var_array_set(&env->vars, name, value);
	env->sorted = FALSE;
}

void
ni_testbus_unsetenv(ni_testbus_env_t *env, const char *name)
{
	ni_var_array_set(&env->vars, name, NULL);
	env->sorted = FALSE;
}

const char *
ni_testbus_getenv(ni_testbus_env_t *env, const char *name)
{
	ni_var_t *var;

	if ((var = ni_var_array_get(&env->vars, name)) && var->value != NULL)
		return var->value;

	return NULL;
}

void
ni_testbus_env_array_init(ni_testbus_env_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_env_array_destroy(ni_testbus_env_array_t *array)
{
	free(array->data);
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_env_array_append(ni_testbus_env_array_t *array, ni_testbus_env_t *env)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = env;
}

/*
 * Sort an env array
 */
static void
__ni_testbus_env_sort(ni_testbus_env_t *env)
{
	if (env->sorted)
		return;
	ni_var_array_sort(&env->vars);
	env->sorted = TRUE;
}

/*
 * Merge several environments into one.
 * We do an n-way sorted tape merge.
 */
struct env_merge_input {
	const ni_var_array_t *array;
	unsigned int next;
};

static inline void
__ni_testbus_env_merge_skip_dups(struct env_merge_input *m, const char *name)
{
	unsigned int nvars = m->array->count;
	ni_var_t *data = m->array->data;

	while (m->next < nvars) {
		int r;

		r = strcmp(data[m->next].name, name);
		if (r < 0)
			ni_fatal("bad: missed variable \"%s\"", data[m->next].name);
		if (r > 0)
			break;
		m->next++;
	}
}

static inline ni_var_t *
__ni_testbus_env_merge_next(struct env_merge_input *m, ni_var_t *best)
{
	unsigned int nvars = m->array->count;
	ni_var_t *next;

	if (m->next >= nvars)
		return best;

	next = &m->array->data[m->next];
	if (best == NULL || strcmp(next->name, best->name) < 0)
		return next;

	return best;
}

ni_bool_t
ni_testbus_env_merge(ni_testbus_env_t *result, ni_testbus_env_array_t *env_array)
{
	struct env_merge_input *merge;
	ni_var_t *last = NULL;
	unsigned int n;

	merge = ni_calloc(env_array->count, sizeof(*merge));
	for (n = 0; n < env_array->count; ++n) {
		__ni_testbus_env_sort(env_array->data[n]);
		merge[n].array = &env_array->data[n]->vars;
	}

	while (TRUE) {
		ni_var_t *var = NULL;
		struct env_merge_input *m;
		unsigned int n;

		for (n = 0, m = merge; n < env_array->count; ++n, ++m) {
			if (last)
				__ni_testbus_env_merge_skip_dups(m, last->name);

			var = __ni_testbus_env_merge_next(m, var);
		}

		if (var == NULL)
			break;

		ni_testbus_setenv(result, var->name, var->value);
		last = var;
	}

	free(merge);
	result->sorted = TRUE;
	return TRUE;
}

