# Open Watcom wmake makefile for jxl2pdf. Not built or run yet: see the README.
#   wmake -f watcom.mak        both targets
#   wmake -f watcom.mak dos    jxl2pdf.exe  (32-bit DOS, DOS/32A extender)
#   wmake -f watcom.mak nt     jxl2pdfw.exe (Win32 console)
#   wmake -f watcom.mak clean

CFLAGS = -q -za99 -5r -ox -w3

all: dos nt .SYMBOLIC

dos: jxl2pdf.exe .SYMBOLIC

nt: jxl2pdfw.exe .SYMBOLIC

jxl2pdf.exe: jxl2pdf.c j40.h
	wcl386 $(CFLAGS) -bt=dos -i="$(%WATCOM)/h" -l=dos32a -k1048576 -fo=jxl2pdf.obj -fe=$@ jxl2pdf.c

jxl2pdfw.exe: jxl2pdf.c j40.h
	wcl386 $(CFLAGS) -bt=nt -i="$(%WATCOM)/h;$(%WATCOM)/h/nt" -l=nt -fo=jxl2pdfw.obj -fe=$@ jxl2pdf.c

clean: .SYMBOLIC
	rm -f jxl2pdf.exe jxl2pdfw.exe jxl2pdf.obj jxl2pdfw.obj *.err
