# Copyright (C) 2010 Krzysztof Foltman. All rights reserved.

CFLAGS += -Wall -O3

LIBS = -ljack

OBJS = main.o io.o module.o

all: cbox

main.o: io.h dspmath.h module.h

module.o: module.h

io.o: io.h

cbox: $(OBJS)
	$(CC) -o cbox $(OBJS) $(LIBS)

clean:
	rm -f *.o