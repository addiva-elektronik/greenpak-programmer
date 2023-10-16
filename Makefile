all: greenpak-programmer

LDLIBS = -li2c
CFLAGS = -Og -g

greenpak-programmer: greenpak-programmer.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)


clean:
	rm -f greenpak-programmer *.o *~ *.bak

install: greenpak-programmer
	install -d $(DESTDIR)/bin
	install greenpak-programmer $(DESTDIR)/bin/greenpak-programmer

