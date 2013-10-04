
#include <dborb/logging.h>
#include <testbus/model.h>
#include <dborb/dbus-model.h>


#define DEFINE_CLASS_FUNCTION(name, dbus_class) \
const ni_dbus_class_t *								\
ni_testbus_##name##_class(void)							\
{										\
	static const ni_dbus_class_t *class = NULL;				\
										\
	if (class == NULL) {							\
		class = ni_objectmodel_get_class(dbus_class);			\
		ni_assert(class);						\
	}									\
	return class;								\
}

DEFINE_CLASS_FUNCTION(hostlist,		NI_TESTBUS_HOSTLIST_CLASS);
DEFINE_CLASS_FUNCTION(container,	NI_TESTBUS_CONTEXT_CLASS);
DEFINE_CLASS_FUNCTION(command,		NI_TESTBUS_COMMAND_CLASS);
DEFINE_CLASS_FUNCTION(host,		NI_TESTBUS_HOST_CLASS);
DEFINE_CLASS_FUNCTION(agent,		NI_TESTBUS_AGENT_CLASS);
DEFINE_CLASS_FUNCTION(filesystem,	NI_TESTBUS_FILESYSTEM_CLASS);
DEFINE_CLASS_FUNCTION(fileset,		NI_TESTBUS_FILESET_CLASS);
DEFINE_CLASS_FUNCTION(tmpfile,		NI_TESTBUS_TMPFILE_CLASS);
DEFINE_CLASS_FUNCTION(testset,		NI_TESTBUS_TESTSET_CLASS);
DEFINE_CLASS_FUNCTION(testcase,		NI_TESTBUS_TESTCASE_CLASS);
