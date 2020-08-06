CC = g++

# Uncomment one of the following to switch between debug and opt mode
OPT = -O3 -DNDEBUG
#OPT = -g -ggdb

CFLAGS += --std=c++11 -fno-strict-aliasing -Wall -c -I. -I./include -I/usr/include/ -I./include/ $(OPT)

LDFLAGS+= -Wall

HEADERS = $(wildcard include/*.h)

TEST = test

all: $(TEST)

clean:
	rm -f $(TEST) */*.o

test: example/test.o
	$(CC) example/test.o $(LDFLAGS) -o $@

%.o: %.cc ${HEADERS} Makefile
	$(CC) $(CFLAGS) $< -o $@
