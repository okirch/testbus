

#include <sys/wait.h>

#include <dborb/process.h>
#include <testbus/process.h>

void
ni_testbus_process_get_exit_info(const ni_process_t *pi, ni_testbus_process_exit_status_t *exit_info)
{
	memset(exit_info, 0, sizeof(*exit_info));
	if (WIFEXITED(pi->status)) {
		exit_info->how = NI_TESTBUS_PROCESS_EXITED;
		exit_info->exit.code = WEXITSTATUS(pi->status);
	} else
	if (WIFSIGNALED(pi->status)) {
		exit_info->how = NI_TESTBUS_PROCESS_CRASHED;
		exit_info->crash.signal = WTERMSIG(pi->status);
		exit_info->crash.core_dumped = !!WCOREDUMP(pi->status);
	} else {
		exit_info->how = NI_TESTBUS_PROCESS_TRANSCENDED;
	}
}

ni_bool_t
ni_testbus_process_serialize(const ni_process_t *pi, ni_dbus_variant_t *dict)
{
	ni_dbus_dict_add_string_array(dict, "argv", (const char **) pi->argv.data, pi->argv.count);
	ni_dbus_dict_add_string_array(dict, "env", (const char **) pi->environ.data, pi->environ.count);

	/* TBD: file information */

	return TRUE;
}

ni_process_t *
ni_testbus_process_deserialize(const ni_dbus_variant_t *dict)
{
	const ni_dbus_variant_t *e;
	ni_process_t *pi;

	/* Create a new process instance, and set the default environment. */
	pi = ni_process_new(TRUE);

	if (!(e = ni_dbus_dict_get(dict, "argv"))
	 || !ni_dbus_variant_get_string_array(e, &pi->argv))
		goto failed;

	if (!(e = ni_dbus_dict_get(dict, "env"))
	 || !ni_dbus_variant_get_string_array(e, &pi->environ))
		goto failed;

	/* TBD: file information */

	return pi;

failed:
	ni_process_free(pi);
	return NULL;
}

ni_bool_t
ni_testbus_process_exit_info_serialize(const ni_testbus_process_exit_status_t *exit_info, ni_dbus_variant_t *dict)
{
	switch (exit_info->how) {
	case NI_TESTBUS_PROCESS_EXITED:
		ni_dbus_dict_add_uint32(dict, "exit-code", exit_info->exit.code);
		break;
	case NI_TESTBUS_PROCESS_CRASHED:
		ni_dbus_dict_add_uint32(dict, "exit-signal", exit_info->crash.signal);
		ni_dbus_dict_add_bool(dict, "core-dumped", exit_info->crash.core_dumped);
		break;
	default:
		/* Nothing */ ;
	}

	/* For now, we're not capturing stdout/stderr */
	ni_dbus_dict_add_uint32(dict, "stdout-total-bytes", 0);
	ni_dbus_dict_add_uint32(dict, "stderr-total-bytes", 0);
	return TRUE;
}

ni_testbus_process_exit_status_t *
ni_testbus_process_exit_info_deserialize(const ni_dbus_variant_t *dict)
{
	ni_testbus_process_exit_status_t *exit_info;
	uint32_t u32;
	dbus_bool_t b;

	exit_info = ni_calloc(1, sizeof(*exit_info));

	if (ni_dbus_dict_get_uint32(dict, "exit-code", &u32)) {
		exit_info->how = NI_TESTBUS_PROCESS_EXITED;
		exit_info->exit.code = u32;
	} else
	if (ni_dbus_dict_get_uint32(dict, "exit-signal", &u32)) {
		exit_info->how = NI_TESTBUS_PROCESS_CRASHED;
		exit_info->crash.signal = u32;
		if (ni_dbus_dict_get_bool(dict, "core-dumped", &b))
			exit_info->crash.core_dumped = b;
	} else {
		exit_info->how = NI_TESTBUS_PROCESS_TRANSCENDED;
	}

	/* TBD: stderr/stdout signaling */

	return exit_info;
}

