
#ifndef __AGENT_FILES_H__
#define __AGENT_FILES_H__

#include <testbus/file.h>

extern ni_bool_t	ni_testbus_agent_process_attach_files(ni_process_t *, ni_testbus_file_array_t *);
extern ni_bool_t	ni_testbus_agent_process_export_files(ni_process_t *, ni_testbus_file_array_t *);
extern void		ni_testbus_agent_discard_cached_file(const char *);

#endif /* __AGENT_FILES_H__ */
