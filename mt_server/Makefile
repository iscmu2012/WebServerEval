EXE = server fastcgi libhello.so libsumnumbers.so
CC = gcc
CFLAGS = -g -Wall -rdynamic
PFLAGS = -lpthread
LDFLAGS = -ldl
DEPS = csapp.h sbuf.h common.h ed_epoll_event.h ed_http.h ed_server.h fastcgi_server.h 
WEBOBJ = csapp.o sbuf.o common.o ed_epoll_event.o ed_http.o ed_server.o 
FASTCGIOBJ = common.o fastcgi_server.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) $(PFLAGS)

all: $(EXE)

server: $(WEBOBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(PFLAGS)

fastcgi: $(FASTCGIOBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(PFLAGS) $(LDFLAGS)

hello.o: hello.c
	$(CC) -fpic -c hello.c $(CFLAGS)

libhello.so: hello.o
	$(CC) -shared -Wl,-soname,libhello.so hello.o -lc -o $@

sumnumbers.o: sumnumbers.c
	$(CC) -fpic -c sumnumbers.c $(CFLAGS)

libsumnumbers.so: sumnumbers.o
	$(CC) -shared -Wl,-soname,libsumnumbers.so sumnumbers.o -lc -o $@

.PHONY: clean

clean:
	rm -f *.o $(EXE)
