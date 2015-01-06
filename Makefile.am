#maintainer-clean removes everything
MAINTAINERCLEANFILES = aclocal.m4 compile config.sub config.guess config.h.in \
		       configure depcomp install-sh ltmain.sh Makefile.in missing

SUBDIRS = src \
          data

filesdir = $(datadir)

uninstall:
	rm -rf $(DESTDIR)$(datadir)

