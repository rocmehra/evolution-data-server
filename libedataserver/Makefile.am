INCLUDES =								\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server\"				\
	$(DB_CFLAGS)							\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"				\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"			\
	-DE_DATA_SERVER_EXTENSIONDIR=\"$(extensiondir)\"		\
	-DE_DATA_SERVER_IMAGESDIR=\"$(imagesdir)\"			\
	-DE_DATA_SERVER_UI_GLADEDIR=\""$(gladedir)"\"			\
	$(E_DATA_SERVER_CFLAGS)						\
	$(SOUP_CFLAGS)

lib_LTLIBRARIES = libedataserver-1.2.la
noinst_PROGRAMS = test-source-list

libedataserver_1_2_la_SOURCES =		\
	e-account-list.c		\
	e-account.c			\
	e-categories.c			\
	e-component-listener.c		\
	e-data-server-module.c		\
	e-dbhash.c			\
	e-db3-utils.c			\
	e-file-cache.c			\
	e-flag.c			\
	e-iconv.c			\
	e-iterator.c			\
	e-list.c			\
	e-list-iterator.c		\
	e-memory.c			\
	e-msgport.c			\
	e-proxy.c			\
	e-sexp.c			\
	e-source-group.c		\
	e-source-list.c			\
	e-source.c			\
	e-time-utils.c			\
	e-uid.c				\
	e-url.c				\
	e-data-server-util.c		\
	e-trie.c			\
	e-xml-utils.c			\
	e-xml-hash-utils.c		\
	libedataserver-private.h	\
	md5-utils.c

libedataserver_1_2_la_LIBADD = 				\
	$(E_DATA_SERVER_LIBS)				\
	$(ICONV_LIBS)					\
	$(DB_LIBS)					\
	$(SOCKET_LIBS)					\
	$(SOUP_LIBS)

libedataserver_1_2_la_LDFLAGS = \
	-version-info $(LIBEDATASERVER_CURRENT):$(LIBEDATASERVER_REVISION):$(LIBEDATASERVER_AGE) $(NO_UNDEFINED)

libedataserverincludedir = $(privincludedir)/libedataserver

libedataserverinclude_HEADERS =		\
	e-account-list.h		\
	e-account.h			\
	e-categories.h			\
	e-component-listener.h		\
	e-data-server-module.h		\
	e-db3-utils.h			\
	e-dbhash.h			\
	e-file-cache.h			\
	e-flag.h			\
	e-iconv.h			\
	e-iterator.h			\
	e-list.h			\
	e-list-iterator.h		\
	e-memory.h			\
	e-msgport.h			\
	e-proxy.h			\
	e-sexp.h			\
	e-source-group.h		\
	e-source-list.h			\
	e-source.h			\
	e-time-utils.h			\
	e-trie.h			\
	e-uid.h				\
	e-url.h				\
	e-data-server-util.h		\
	e-xml-utils.h			\
	e-xml-hash-utils.h		\
	md5-utils.h

test_source_list_SOURCES = test-source-list.c
test_source_list_LDADD = libedataserver-1.2.la $(E_DATA_SERVER_LIBS)

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserver-$(API_VERSION).pc

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)
