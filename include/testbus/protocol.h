#ifndef __NI_TESTBUS_PROTOCOL_H__
#define __NI_TESTBUS_PROTOCOL_H__

#define NI_TESTBUS_NAMESPACE		"org.opensuse.Testbus"
#define NI_TESTBUS_OBJECT_ROOT		"/org/opensuse/Testbus"

#define NI_TESTBUS_DBUS_BUS_NAME	NI_TESTBUS_NAMESPACE
#define NI_TESTBUS_OBJECT_PATH		NI_TESTBUS_OBJECT_ROOT

#define NI_TESTBUS_ROOT_PATH		NI_TESTBUS_OBJECT_ROOT

/*
 * Object:
 *	/GlobalContext
 * Interfaces:
 *	any context interface
 */
#define NI_TESTBUS_GLOBAL_CONTEXT_PATH	NI_TESTBUS_OBJECT_ROOT "/GlobalContext"

/*
 * Object:
 *	/Hosts
 * Interfaces:
 *	HostList
 */
#define NI_TESTBUS_HOSTLIST_PATH	NI_TESTBUS_OBJECT_ROOT "/Host"

/*
 * Object:
 *	/Host/<seq>
 * Interfaces:
 *	Host
 *	Environment
 *	CommandQueue
 *	Fileset
 */
#define NI_TESTBUS_HOST_BASE_PATH	NI_TESTBUS_HOSTLIST_PATH

/*
 * Object:
 *	/Tmpfile/<seq>
 * Interfaces:
 *	Tmpfile
 */
#define NI_TESTBUS_FILE_BASE_PATH	NI_TESTBUS_OBJECT_ROOT "/Tmpfile"

/*
 * Object:
 *	/Command/<seq>
 * Interfaces:
 *	Command
 *	Environment
 */
#define NI_TESTBUS_COMMAND_BASE_PATH	NI_TESTBUS_OBJECT_ROOT "/Command"

/*
 * Object:
 *	/Agent
 * Interfaces:
 *	Agent
 */
#define NI_TESTBUS_AGENT_BASE_PATH	NI_TESTBUS_OBJECT_ROOT "/Agent"


/*
 * Object:
 *	/Agent/Files
 * Interfaces:
 *	Agent.Filesystem
 */
#define NI_TESTBUS_AGENT_FS_PATH	NI_TESTBUS_AGENT_BASE_PATH "/Filesystem"


#define NI_TESTBUS_ROOT_INTERFACE	NI_TESTBUS_NAMESPACE

/*
 * Interface:
 *	HostList
 * Methods:
 *	createHost(name)
 *	reconnect(name, uuid)
 * Compatible with class:
 *	hostlist
 */
#define NI_TESTBUS_HOSTLIST_INTERFACE	NI_TESTBUS_NAMESPACE ".HostList"

/*
 * Interface:
 *	Hostset
 * Methods:
 *	addHost(role, path)
 * Compatible with class:
 *	hostset -> container
 */
#define NI_TESTBUS_HOSTSET_INTERFACE	NI_TESTBUS_NAMESPACE ".Hostset"

/*
 * Interface:
 *	Container
 * Methods:
 *	...
 * Compatible with class:
 *	container
 */
#define NI_TESTBUS_CONTAINER_INTERFACE	NI_TESTBUS_NAMESPACE ".Container"

/*
 * Interface:
 *	Host
 * Methods:
 *	reboot()
 * Compatible with class:
 *	host -> container
 */
#define NI_TESTBUS_HOST_INTERFACE	NI_TESTBUS_NAMESPACE ".Host"

/*
 * Interface:
 *	Eventlog
 * Methods:
 * Compatible with class:
 *	host -> container
 */
#define NI_TESTBUS_EVENTLOG_INTERFACE	NI_TESTBUS_NAMESPACE ".Eventlog"

/*
 * Interface:
 *	Environment
 * Methods:
 *	setenv(name, value)
 * Compatible with class:
 *	environ -> container
 */
#define NI_TESTBUS_ENVIRON_INTERFACE	NI_TESTBUS_NAMESPACE ".Environment"

/*
 * Interface:
 *	CommandQueue
 * Methods:
 *	createCommand(argv)
 * Compatible with class:
 *	command-queue -> container
 */
#define NI_TESTBUS_CMDQUEUE_INTERFACE	NI_TESTBUS_NAMESPACE ".CommandQueue"

/*
 * Interface:
 *	Command
 * Methods:
 *	run()
 * Compatible with class:
 *	command -> container
 */
#define NI_TESTBUS_COMMAND_INTERFACE	NI_TESTBUS_NAMESPACE ".Command"

/*
 * Interface:
 *	Process
 * Methods:
 *	kill()
 * Compatible with class:
 *	process -> container
 */
#define NI_TESTBUS_PROCESS_INTERFACE	NI_TESTBUS_NAMESPACE ".Process"

/*
 * Interface:
 *	File
 * Methods:
 *	-
 * Compatible with class:
 *	tmpfile
 */
#define NI_TESTBUS_TMPFILE_INTERFACE	NI_TESTBUS_NAMESPACE ".Tmpfile"

/*
 * Interface:
 *	Fileset
 * Methods:
 *	createFile(name)
 * Compatible with class:
 *	fileset -> container
 */
#define NI_TESTBUS_FILESET_INTERFACE	NI_TESTBUS_NAMESPACE ".Fileset"

/*
 * Interface:
 *	Testset
 * Methods:
 *	createTest(name)
 * Compatible with class:
 *	testset -> container
 */
#define NI_TESTBUS_TESTSET_INTERFACE	NI_TESTBUS_NAMESPACE ".Testset"

/*
 * Interface:
 *	Testcase
 * Methods:
 *	...
 * Compatible with class:
 *	testcase
 */
#define NI_TESTBUS_TESTCASE_INTERFACE	NI_TESTBUS_INTERFACE ".Testcase"

/*
 * Interface:
 *	Agent
 * Methods:
 *	...
 * Compatible with class:
 *	agent
 */
#define NI_TESTBUS_AGENT_INTERFACE	NI_TESTBUS_NAMESPACE ".Agent"

/*
 * Interface:
 *	Agent.Filesystem
 * Methods:
 *	...
 * Compatible with class:
 *	filesystem
 */
#define NI_TESTBUS_AGENT_FS_INTERFACE	NI_TESTBUS_AGENT_INTERFACE ".Filesystem"

/*
 * Object classes used in the testbus model
 */
#define NI_TESTBUS_HOSTLIST_CLASS	"hostlist"
#define NI_TESTBUS_HOST_CLASS		"host"
#define NI_TESTBUS_ENVIRON_CLASS	"environ"
#define NI_TESTBUS_CONTEXT_CLASS	"context"
#define NI_TESTBUS_COMMAND_CLASS	"command"
#define NI_TESTBUS_CMDQUEUE_CLASS	"command-queue"
#define NI_TESTBUS_AGENT_CLASS		"agent"
#define NI_TESTBUS_FILESYSTEM_CLASS	"filesystem"
#define NI_TESTBUS_FILESET_CLASS	"fileset"
#define NI_TESTBUS_FILE_CLASS		"file"
#define NI_TESTBUS_TESTSET_CLASS	"testset"
#define NI_TESTBUS_HOSTSET_CLASS	"hostset"
#define NI_TESTBUS_TESTCASE_CLASS	"testcase"
#define NI_TESTBUS_PROCESS_CLASS	"process"

#endif /* __NI_TESTBUS_PROTOCOL_H__ */
