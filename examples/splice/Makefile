CC	= gcc
CFLAGS	= -Wall -O2 -g -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
PROGS	= ktee ktee-net splice-cp splice-in splice-out splice-tonet splice-fromnet splice-test4c splice-test4s vmsplice splice-bench vmsplice2 vmsplice-touser splice-fromnet
MANS	= splice.2 tee.2 vmsplice.2

all: depend $(PROGS)
	$(MAKE) -C nettest

%.o: %.c
	$(CC) -o $*.o -c $(CFLAGS) $<

depend:
	@$(CC) -MM $(CFLAGS) *.c 1> .depend

clean:
	-rm -f *.o $(PROGS) .depend
	$(MAKE) -C nettest clean

INSTALL = install
prefix = /usr/local
bindir = $(prefix)/bin
mandir = $(prefix)/man

install: $(PROGS)
	$(INSTALL) -m755 -d $(DESTDIR)$(bindir)
	$(INSTALL) $(PROGS) $(DESTDIR)$(bindir)
	$(INSTALL) -m755 -d $(DESTDIR)$(mandir)/man2
	$(INSTALL) $(MANS) $(DESTDIR)$(mandir)/man2

ifneq ($(wildcard .depend),)
include .depend
endif
