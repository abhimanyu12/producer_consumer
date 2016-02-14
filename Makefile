CC=gcc
CFLAGS=-Wall -Wextra -std=c99 -pedantic -Wunreachable-code -O2
LDFLAGS=-lpthread

OBJECTS=pc.o
SOURCE=pc.c

pc: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o pc $(LDFLAGS)

all:pc
clean:
	rm -f *~ *.o *.out pc
