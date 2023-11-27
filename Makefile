all: greenpak-programmer

PACKAGE = greenpak-programmer
VERSION = 1.2

LDLIBS = -li2c
CFLAGS = -Og -g

$(PACKAGE): $(PACKAGE).c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)


clean:
	rm -f $(PACKAGE) *.o *~ *.bak

install: $(PACKAGE)
	install -d $(DESTDIR)/bin
	install $(PACKAGE) $(DESTDIR)/bin/$(PACKAGE)

uninstall:
	$(RM) -f $(DESTDIR)/bin/$(PACKAGE)

dist:
	@git archive --format=tar --prefix=$(PACKAGE)-$(VERSION)/ v$(VERSION) | gzip >../$(PACKAGE)-$(VERSION).tar.gz
	@(cd .. && md5sum    $(PACKAGE)-$(VERSION).tar.gz > $(PACKAGE)-$(VERSION).tar.gz.md5)
	@(cd .. && sha256sum $(PACKAGE)-$(VERSION).tar.gz > $(PACKAGE)-$(VERSION).tar.gz.sha256)

release: dist
	@echo "Resulting release files in parent dir:"
	@echo "=================================================================================================="
	@for file in $(PACKAGE)-$(VERSION).tar.gz; do					\
		printf "%-33s Distribution tarball\n" $$file;                           \
		printf "%-33s " $$file.md5;    cat ../$$file.md5    | cut -f1 -d' ';    \
		printf "%-33s " $$file.sha256; cat ../$$file.sha256 | cut -f1 -d' ';    \
	done
