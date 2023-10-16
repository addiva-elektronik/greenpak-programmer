all: greenpak_programmer

LDLIBS = -li2c
CFLAGS = -Og -g

greenpak_programmer: greenpak_programmer.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)


clean:
	rm -f greenpak_programmer *.o *~ *.bak

install: greenpak_programmer
	install -d $(DESTDIR)/bin
	install greenpak_programmer $(DESTDIR)/bin/greenpak_programmer

