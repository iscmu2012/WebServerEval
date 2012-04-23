FLASHDEFS = 	-DDEFAULT_PORTSTR="\"31415\""
FLASH =		flash
FL_CONV = 	convert_slave
FL_READ = 	read_slave_mmap
FL_DIR =	dir_slave
EXTRA_SRC = 	dummycgi.c gscgi.c
EXTRA_TARG = 	d_reg
CLEANING = 	dummycgi.o gscgi.o d_reg
include MakeInfo

d_reg: dummycgi.o gscgi.o
	$(CC) -o $@ dummycgi.o gscgi.o $(LIBS); cp -f $@ cgi-bin;
