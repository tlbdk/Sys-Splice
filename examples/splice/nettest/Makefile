#CC	= /opt/intel/cce/9.1.045/bin/icc
CC	= gcc
OPTFLAGS= -O2 -g $(EXTFLAGS)
CFLAGS	= -Wall -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 $(OPTFLAGS)
PROGS	= xmit recv sf fillfile

all: $(PROGS)

xmit: crc32.o xmit.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^)
recv: crc32.o recv.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^)

sf: sf.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^)

fillfile: crc32.o fillfile.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^)

clean:
	-rm -f *.o .depend cscope.out $(PROGS) core.* core
depend:
	@$(CC) -MM $(ALL_CFLAGS) *.c 1> .depend

$(PROGS): depend

ifneq ($(wildcard .depend),)
include .depend
endif
