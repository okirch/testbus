
#include <dborb/logging.h>
#include "monitor.h"

static void			ni_testbus_monitor_destroy(ni_testbus_container_t *);
static void			ni_testbus_monitor_free(ni_testbus_container_t *);

static struct ni_testbus_container_ops ni_testbus_monitor_ops = {
	.features		= 0,
	.dbus_name_prefix	= "Monitor",

	.destroy		= ni_testbus_monitor_destroy,
	.free			= ni_testbus_monitor_free,
};

ni_testbus_monitor_t *
ni_testbus_monitor_new(ni_testbus_container_t *parent, const char *name, const char *class, ni_var_array_t *params)
{
	ni_testbus_monitor_t *mon;

	ni_assert(parent);
	ni_assert(ni_testbus_container_has_monitors(parent));

	mon = ni_malloc(sizeof(*mon));
	ni_var_array_copy(&mon->params, params);
	ni_string_dup(&mon->class, class);

	ni_testbus_container_init(&mon->context,
			&ni_testbus_monitor_ops,
			name,
			parent);

	return mon;
}


void
ni_testbus_monitor_destroy(ni_testbus_container_t *container)
{
	ni_testbus_monitor_t *mon = ni_testbus_monitor_cast(container);

	ni_var_array_destroy(&mon->params);
	ni_string_free(&mon->class);
}

void
ni_testbus_monitor_free(ni_testbus_container_t *container)
{
	ni_testbus_monitor_t *mon = ni_testbus_monitor_cast(container);

	free(mon);
}


ni_bool_t
ni_testbus_container_isa_monitor(const ni_testbus_container_t *container)
{
	return container->ops == &ni_testbus_monitor_ops;
}

ni_testbus_monitor_t *
ni_testbus_monitor_cast(ni_testbus_container_t *container)
{
	ni_assert(container->ops == &ni_testbus_monitor_ops);
	return ni_container_of(container, ni_testbus_monitor_t, context);
}

void
ni_testbus_monitor_array_init(ni_testbus_monitor_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_monitor_array_destroy(ni_testbus_monitor_array_t *array)
{
	while (array->count) {
		ni_testbus_monitor_t *mon = array->data[--(array->count)];

		mon->context.parent = NULL;
		ni_testbus_monitor_put(mon);
	}

	free(array->data);
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_monitor_array_append(ni_testbus_monitor_array_t *array, ni_testbus_monitor_t *monitor)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_monitor_get(monitor);
}

int
ni_testbus_monitor_array_index(ni_testbus_monitor_array_t *array, const ni_testbus_monitor_t *monitor)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		if (array->data[i] == monitor)
			return i;
	}
	return -1;
}

ni_testbus_monitor_t *
ni_testbus_monitor_array_take_at(ni_testbus_monitor_array_t *array, unsigned int index)
{
	ni_testbus_monitor_t *taken;

	if (index >= array->count)
		return NULL;

	taken = array->data[index];

	memmove(&array->data[index], &array->data[index+1], (array->count - (index + 1)) * sizeof(array->data[0]));
	array->count --;

	return taken;
}

ni_bool_t
ni_testbus_monitor_array_remove(ni_testbus_monitor_array_t *array, const ni_testbus_monitor_t *monitor)
{
	ni_testbus_monitor_t *taken;
	int index;

	if ((index = ni_testbus_monitor_array_index(array, monitor)) < 0
	 || (taken = ni_testbus_monitor_array_take_at(array, index)) == NULL)
		return FALSE;

	/* Drop the reference to the monitor */
	ni_testbus_monitor_put(taken);
	return TRUE;
}
