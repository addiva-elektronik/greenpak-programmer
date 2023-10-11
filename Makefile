all: greenpak_programmer

LDLIBS = -li2c
CFLAGS = -Og -g

greenpak_programmer: greenpak_programmer.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o greenpak_programmer $< $(LDFLAGS) $(LDLIBS)


clean:
	rm -f greenpak_programmer *.o *~ *.bak
