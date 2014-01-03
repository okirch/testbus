

#include <sys/wait.h>

#include <dborb/process.h>
#include <testbus/process.h>

ni_bool_t
ni_testbus_process_serialize(const ni_process_t *pi, ni_dbus_variant_t *dict)
{
	ni_dbus_variant_init_dict(dict);
	ni_dbus_dict_add_string_array(dict, "argv", (const char **) pi->argv.data, pi->argv.count);
	ni_dbus_dict_add_string_array(dict, "env", (const char **) pi->environ.data, pi->environ.count);
	ni_dbus_dict_add_bool(dict, "use-terminal", pi->use_terminal);

	return TRUE;
}

ni_process_t *
ni_testbus_process_deserialize(const ni_dbus_variant_t *dict)
{
	const ni_dbus_variant_t *e;
	ni_process_t *pi;
	dbus_bool_t bval;

	/* Create a new process instance, and set the default environment. */
	pi = ni_process_new(TRUE);

	if (!(e = ni_dbus_dict_get(dict, "argv"))
	 || !ni_dbus_variant_get_string_array(e, &pi->argv))
		goto failed;

	if (!(e = ni_dbus_dict_get(dict, "env"))
	 || !ni_dbus_variant_get_string_array(e, &pi->environ))
		goto failed;

	if (ni_dbus_dict_get_bool(dict, "use-terminal", &bval))
		pi->use_terminal = bval;

	return pi;

failed:
	ni_process_free(pi);
	return NULL;
}

ni_bool_t
ni_testbus_process_exit_info_serialize(const ni_process_exit_info_t *exit_info, ni_dbus_variant_t *dict)
{
	ni_dbus_variant_init_dict(dict);

	ni_dbus_dict_add_uint32(dict, "how", exit_info->how);

	switch (exit_info->how) {
	case NI_PROCESS_EXITED:
		ni_dbus_dict_add_uint32(dict, "exit-code", exit_info->exit.code);
		break;
	case NI_PROCESS_CRASHED:
		ni_dbus_dict_add_uint32(dict, "exit-signal", exit_info->crash.signal);
		ni_dbus_dict_add_bool(dict, "core-dumped", exit_info->crash.core_dumped);
		break;
	default:
		/* Nothing */ ;
	}

	/* For now, we're not capturing stdout/stderr */
	ni_dbus_dict_add_uint32(dict, "stdout-total-bytes", exit_info->stdout_bytes);
	ni_dbus_dict_add_uint32(dict, "stderr-total-bytes", exit_info->stderr_bytes);
	return TRUE;
}

ni_process_exit_info_t *
ni_testbus_process_exit_info_deserialize(const ni_dbus_variant_t *dict)
{
	ni_process_exit_info_t *exit_info;
	uint32_t u32;
	dbus_bool_t b;

	exit_info = ni_calloc(1, sizeof(*exit_info));

	exit_info->how = NI_PROCESS_TRANSCENDED;
	if (ni_dbus_dict_get_uint32(dict, "how", &u32))
		exit_info->how = u32;

	if (ni_dbus_dict_get_uint32(dict, "exit-code", &u32)) {
		exit_info->how = NI_PROCESS_EXITED;
		exit_info->exit.code = u32;
	} else
	if (ni_dbus_dict_get_uint32(dict, "exit-signal", &u32)) {
		exit_info->how = NI_PROCESS_CRASHED;
		exit_info->crash.signal = u32;
		if (ni_dbus_dict_get_bool(dict, "core-dumped", &b))
			exit_info->crash.core_dumped = b;
	}

	if (ni_dbus_dict_get_uint32(dict, "stdout-total-bytes", &u32))
		exit_info->stdout_bytes = u32;
	if (ni_dbus_dict_get_uint32(dict, "stderr-total-bytes", &u32))
		exit_info->stderr_bytes = u32;

	return exit_info;
}

