EXE = server fastcgi libhello.so libsumnumbers.so libgenerate_text.so
CC = gcc
CFLAGS = -g -Wall -rdynamic
LDFLAGS = -lpthread -ldl
DEPS = common.h ed_epoll_event.h ed_http.h ed_server.h fastcgi_server.h
WEBOBJ = common.o ed_epoll_event.o ed_http.o ed_server.o
FASTCGIOBJ = common.o fastcgi_server.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(EXE)

server: $(WEBOBJ)
	$(CC) -o $@ $^ $(CFLAGS)

fastcgi: $(FASTCGIOBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

generate_text.o: generate_text.c
	$(CC) -fpic -c generate_text.c $(CFLAGS)

libgenerate_text.so: generate_text.o
	$(CC) -shared -Wl,-soname,libgenerate_text.so generate_text.o -lc -o $@

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
