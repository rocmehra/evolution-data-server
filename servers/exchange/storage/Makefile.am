# Fix the code to not use E_DATA_SERVER_UI_CFLAGS

if OS_WIN32
WIN32_BOOTSTRAP_LIBS =					\
	$(top_builddir)/win32/libedataserverui-1.2.la
endif

INCLUDES =							\
	-DG_LOG_DOMAIN=\"evolution-exchange-storage\"		\
	-DPREFIX=\"$(prefix)\"					\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DDATADIR=\""$(datadir)"\"	 			\
	-DLIBDIR=\""$(datadir)"\"	 			\
	-DCONNECTOR_LOCALEDIR=\""$(localedir)\""		\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\"			\
	$(KRB5_CFLAGS)						\
	$(LDAP_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	$(E_DATA_SERVER_CFLAGS)					\
	$(E_DATA_SERVER_UI_CFLAGS)				\
	-I$(top_builddir)/servers/exchange/lib			\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/servers/exchange/lib			\
	-I$(top_srcdir)/servers/exchange/xntlm

lib_LTLIBRARIES =						\
	libexchange-storage-1.2.la

libexchange_storage_1_2_la_SOURCES =				\
	e-folder.c						\
	e-folder.h						\
	e-folder-exchange.c					\
	e-folder-exchange.h					\
	e-folder-tree.c						\
	e-folder-tree.h						\
	e-folder-type-registry.c				\
	e-folder-type-registry.h				\
	e-storage.c						\
	e-storage.h						\
	exchange-account.c					\
	exchange-account.h					\
	exchange-esource.c					\
	exchange-esource.h					\
	exchange-folder-size.c					\
	exchange-folder-size.h					\
	exchange-hierarchy-favorites.c				\
	exchange-hierarchy-favorites.h				\
	exchange-hierarchy-foreign.c				\
	exchange-hierarchy-foreign.h				\
	exchange-hierarchy-gal.c				\
	exchange-hierarchy-gal.h				\
	exchange-hierarchy-somedav.c				\
	exchange-hierarchy-somedav.h				\
	exchange-hierarchy-webdav.c				\
	exchange-hierarchy-webdav.h				\
	exchange-hierarchy.c					\
	exchange-hierarchy.h					\
	exchange-oof.c						\
	exchange-oof.h						\
	exchange-types.h					

libexchange_storage_1_2_la_LIBADD =				\
	$(WIN32_BOOTSTRAP_LIBS)					\
	$(top_builddir)/servers/exchange/lib/libexchange.la	\
	$(top_builddir)/servers/exchange/xntlm/libxntlm.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_LIBS)					\
	$(E_DATA_SERVER_UI_LIBS)				\
	$(LDAP_LIBS)						\
	$(SOUP_LIBS)						\
	$(SOCKET_LIBS)

libexchange_storage_1_2_la_LDFLAGS =				\
	$(KRB5_LDFLAGS)						\
	$(NO_UNDEFINED)						\
	-version-info $(LIBEXCHANGE_STORAGE_CURRENT):$(LIBEXCHANGE_STORAGE_REVISION):$(LIBEXCHANGE_STORAGE_AGE)

libexchange_storageincludedir = $(privincludedir)/exchange

libexchange_storageinclude_HEADERS = 				\
	e-folder.h						\
	e-folder-exchange.h					\
	e-storage.h						\
	exchange-account.h					\
	exchange-constants.h					\
	exchange-folder-size.h					\
	exchange-hierarchy.h					\
	exchange-hierarchy-somedav.h				\
	exchange-hierarchy-webdav.h				\
	exchange-hierarchy-foreign.h				\
	exchange-oof.h						\
	exchange-types.h					\
	exchange-esource.h

%-$(API_VERSION).pc: %.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libexchange-storage-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)
