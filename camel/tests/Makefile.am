
SUBDIRS = lib \
	message folder stream smime misc mime-filter

EXTRA_DIST = data

dist-hook:
	rm -rf `find $(distdir)/data -name CVS`
	rm -rf `find $(distdir)/data -name .cvsignore`
