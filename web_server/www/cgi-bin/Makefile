CC = gcc
CFLAGS = -O2 -Wall -I ..

all: adder multiply subtract fibonacci sleep read_file \
			generate_text dns

adder: adder.c
	$(CC) $(CFLAGS) -o adder adder.c

sleep1: sleep.c
	$(CC) $(CFLAGS) -o sleep sleep.c

multiply: multiply.c
	$(CC) $(CFLAGS) -o multiply multiply.c

subtract: subtract.c
	$(CC) $(CFLAGS) -o subtract subtract.c

fibonacci: fibonacci.c
	$(CC) $(CFLAGS) -o fibonacci fibonacci.c

read_file: read_file.c
	$(CC) $(CFLAGS) -o read_file read_file.c

generate_text: generate_text.c
	$(CC) $(CFLAGS) -o generate_text generate_text.c

dns: dns.c
	$(CC) $(CFLAGS) -o dns dns.c

clean:
	rm -f adder multiply subtract fibonacci read_file \
	generate_text dns sleep *~ *.exe
