
ARCH	:= $(shell arch)
ifneq ($(findstring $(ARCH),i586 i686),)
	ARCHLIB = lib
else ifneq ($(findstring $(ARCH),x86_64 s390x ppc64),)
	ARCHLIB = lib64
endif


CC	= gcc
CCOPT	= -g
CPPFLAGS= -D_GNU_SOURCE \
	  -DTESTBUS_CONFIGDIR=\"/etc/testbus\" \
	  -Iinclude -Ilib \
	  -I/usr/include/dbus-1.0 -I/usr/$(ARCHLIB)/dbus-1.0/include
CFLAGS	= $(CWARNFLAGS) $(CCOPT) $(CPPFLAGS)
LINK	= -L. -ltestbus -ldborb \
	  -L/$(ARCHLIB) -ldbus-1 \
	  -lgcrypt \
	  -ldl
CWARNFLAGS= -Wall -Werror

LIBSRCS	= \
	dborb/appconfig.c \
	dborb/buffer.c \
	dborb/dbus-client.c \
	dborb/dbus-common.c \
	dborb/dbus-connection.c \
	dborb/dbus-dict.c \
	dborb/dbus-errors.c \
	dborb/dbus-extension.c \
	dborb/dbus-introspect.c \
	dborb/dbus-lookup.c \
	dborb/dbus-message.c \
	dborb/dbus-model.c \
	dborb/dbus-object.c \
	dborb/dbus-server.c \
	dborb/dbus-xml.c \
	dborb/extension.c \
	dborb/global.c \
	dborb/logging.c \
	dborb/md5sum.c \
	dborb/process.c \
	dborb/socket.c \
	dborb/timer.c \
	dborb/util.c \
	dborb/xml.c \
	dborb/xml-reader.c \
	dborb/xml-schema.c \
	dborb/xml-writer.c \
	dborb/xpath.c

LIBTBSRCS = \
	testbus/calls.c \
	testbus/classes.c \
	testbus/model.c \
	testbus/file.c \
	testbus/process.c

SRVSRCS = \
	server/main.c \
	server/dbus-command.c \
	server/dbus-container.c \
	server/dbus-environ.c \
	server/dbus-fileset.c \
	server/dbus-host.c \
	server/dbus-test.c \
	server/dbus-process.c \
	server/command.c \
	server/container.c \
	server/environ.c \
	server/fileset.c \
	server/host.c \
	server/root.c \
	server/test.c

CLNTSRCS = \
	client/main.c

AGNTSRCS = \
	agent/main.c \
	agent/files.c \
	agent/dbus-filesystem.c

PROXSRCS = \
	proxy/main.c

LIBDBORB= libdborb.a
LIBOBJS	= $(addprefix obj/,$(LIBSRCS:.c=.o))

LIBTESTBUS=libtestbus.a
LIBTBOBJS= $(addprefix obj/,$(LIBTBSRCS:.c=.o))

MASTER	= testbus-master
SRVOBJS	= $(addprefix obj/,$(SRVSRCS:.c=.o))

CLIENT	= testbus-client
CLNTOBJS= $(addprefix obj/,$(CLNTSRCS:.c=.o))

AGENT	= testbus-agent
AGNTOBJS= $(addprefix obj/,$(AGNTSRCS:.c=.o))

PROXY	= dbus-proxy
PROXOBJS= $(addprefix obj/,$(PROXSRCS:.c=.o))

BINTGTS	= $(MASTER) $(CLIENT) $(AGENT) $(PROXY)
LIBTGTS	= $(LIBDBORB) $(LIBTESTBUS)
ALL	= $(LIBTGTS) $(BINTGTS)
LIBDEPS	= $(LIBDBORB) $(LIBTESTBUS)

INSTALL_BINDIRS = \
	/usr/bin \
	/usr/sbin \
	/etc/init.d

INSTALL_DATADIRS = \
	/etc/testbus \
	/etc/dbus-1/system.d \
	/etc/testbus/agent.d \
	/usr/share/testbus \
	/usr/share/testbus/schema \
	/var/run/testbus \
	/var/lib/testbus

all:	$(ALL)

install: install-bin install-data

install-bin: $(ALL)
	install -m755 -d $(addprefix $(DESTDIR),$(INSTALL_BINDIRS))
	install -m555 $(BINTGTS) $(DESTDIR)/usr/bin
	install -m555 etc/master.init $(DESTDIR)/etc/init.d/testbus-master
	ln -fs ../../etc/init.d/testbus-master $(DESTDIR)/usr/sbin/rctestbus-master
	install -m555 etc/agent-kvm.init $(DESTDIR)/etc/init.d/testbus-agent-kvm
	ln -fs ../../etc/init.d/testbus-agent-kvm $(DESTDIR)/usr/sbin/rctestbus-agent-kvm
	install -m555 etc/start-kvm-guest $(DESTDIR)/usr/sbin/testbus-kvm-guest

install-data: etc/org.opensuse.Testbus.conf
	install -m755 -d $(addprefix $(DESTDIR),$(INSTALL_DATADIRS))
	install -m644 etc/config.xml $(DESTDIR)/etc/testbus/config.xml
	install -m555 selftest/functions $(DESTDIR)/usr/share/testbus
	install -m644 schema/*.xml $(DESTDIR)/usr/share/testbus/schema
	install -m755 etc/agent.d/* $(DESTDIR)/etc/testbus/agent.d
	# This is special - dbus-daemon does weird things if we install the file directly.
	install -m644 etc/org.opensuse.Testbus.conf $(DESTDIR)/etc/dbus-1/system.d/org.opensuse.Testbus.conf.new
	mv $(DESTDIR)/etc/dbus-1/system.d/org.opensuse.Testbus.conf{.new,}

distclean clean::
	rm -rf obj core vgcore.*

distclean::
	rm -f $(ALL) .depend

# dborb == dbus object request broker
libdborb.a: $(LIBOBJS)
	rm -f $@
	ar cr $@ $(LIBOBJS)

libtestbus.a: $(LIBTBOBJS)
	rm -f $@
	ar cr $@ $(LIBTBOBJS)

$(MASTER): $(SRVOBJS) $(LIBDEPS)
	$(CC) -o $@ $(SRVOBJS) $(LINK)

$(CLIENT): $(CLNTOBJS) $(LIBDEPS)
	$(CC) -o $@ $(CLNTOBJS) $(LINK)

$(AGENT): $(AGNTOBJS) $(LIBDEPS)
	$(CC) -o $@ $(AGNTOBJS) $(LINK)

$(PROXY): $(PROXOBJS) $(LIBDEPS)
	$(CC) -o $@ $(PROXOBJS) $(LINK)

show-lib-srcs:
	@for n in $(LIBSRCS); do echo $$n; done

depend:
	$(CC) $(CPPFLAGS) -M $(LIBSRCS) | \
		sed 's@^\([^.]*\)\.o: dborb/\([-a-z0-9/]*\)\1.c@obj/dborb/\2&@' > .depend
#	$(CC) $(CPPFLAGS) -M $(LIBSRCS) | \
#		sed 's@^\([^.]*\)\.o: dborb/\([-a-z0-9/]*\)\1.c@obj/shlib/\2&@' > .depend
	$(CC) $(CPPFLAGS) -M $(SRVSRCS) | sed 's:^[a-z]:obj/server/&:' >> .depend
	$(CC) $(CPPFLAGS) -M $(CLNTSRCS) | sed 's:^[a-z]:obj/client/&:' >> .depend
	$(CC) $(CPPFLAGS) -M $(AGNTSRCS) | sed 's:^[a-z]:obj/agent/&:' >> .depend
	$(CC) $(CPPFLAGS) -M $(PROXSRCS) | sed 's:^[a-z]:obj/proxy/&:' >> .depend


obj/dborb/%.o: dborb/%.c
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $(CFLAGS) $<

obj/server/%.o: server/%.c
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $(CFLAGS) $<

obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $(CFLAGS) $<


-include .depend

