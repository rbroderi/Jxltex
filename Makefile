# POSIX make: builds jxl2pdf with the system C compiler. For Open Watcom, see watcom.mak.
#   make           jxl2pdf
#   make check     typeset test/test.tex with each LaTeX engine found
#   make clean

CC ?= cc
CFLAGS ?= -O2

jxl2pdf: jxl2pdf.c j40.h
	$(CC) -std=c99 $(CFLAGS) -o $@ jxl2pdf.c -lm

check: jxl2pdf
	sh test/run.sh

clean:
	rm -f jxl2pdf
	rm -rf test/out

.PHONY: check clean
