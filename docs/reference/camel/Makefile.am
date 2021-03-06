## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE = camel
DOC_MODULE_VERSIONED = $(DOC_MODULE)-$(CAMEL_MAJOR_VERSION).$(CAMEL_MINOR_VERSION)

# The top-level SGML file.
DOC_MAIN_SGML_FILE = camel-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR = $(top_srcdir)/camel

HTML_DIR = $(datadir)/gtk-doc/html

# Images to copy into HTML directory
HTML_IMAGES = 

#TARGET_DIR = $(HTML_DIR)/$(DOC_MODULE_VERSIONED)

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="CAMEL_DISABLE_DEPRECATED"

# Extra options to pass to gtkdoc-scangobj
SCANGOBJ_OPTIONS=--type-init-func="camel_init (\".\", 0)"

#SCANOBJ_FILES = $(DOC_MODULE).hierarchy

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --sgml-mode --output-format=xml --ignore-files=trio

# CFLAGS and LDFLAGS for compiling scan program. Only needed
# if $(DOC_MODULE).types is non-empty.
INCLUDES = \
	-I$(top_srcdir) 	\
	-I$(top_builddir) 	\
	-I$(top_builddir)/camel \
	$(GLIB_CFLAGS)

GTKDOC_LIBS = 					\
	$(top_builddir)/camel/libcamel-2.0.la 	\
	$(GLIB_LIBS)

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE)
content_files = 			\
	tree_index.sgml

# Used for dependencies
HFILE_GLOB=$(top_srcdir)/camel/*.h
CFILE_GLOB=$(top_srcdir)/camel/*.c

# Header files to ignore when scanning
IGNORE_HFILES = 			\
	broken-date-parser.h		\
	camel-charset-map-private.h	\
	camel-private.h			\
	camel-search-private.h		\
	camel-types.h			\
	camel-groupwise-private.h	\
	camel-imap-private.h		\
	camel-imap-types.h		\
	camel-local-private.h		\
	camel-nntp-private.h		\
	camel-nntp-types.h

# Extra options to supply to gtkdoc-fixref
FIXXREF_OPTIONS = 

include $(top_srcdir)/gtk-doc.make

# Other files to distribute
EXTRA_DIST += 				\
	$(content_files)		\
	$(DOC_MODULE)-docs.sgml		\
	$(DOC_MODULE)-sections.txt	\
	camel.hierarchy

########################################################################

BUILT_EXTRA_DIST = 

dist-hook-local: $(BUILT_EXTRA_DIST)
	files='$(BUILT_EXTRA_DIST)';				\
	for f in $$files; do					\
	  if test -f $$f; then d=.; else d=$(srcdir); fi;	\
	  cp $$d/$$f $(distdir) || exit 1; done
