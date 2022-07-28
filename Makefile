CFLAGS = -lm -lz -O3 -msse4.1 -mpclmul -g
CXXFLAGS = -lm -lz -O3 -msse4.1 -mpclmul -g
mud: mud.cpp spng.o fpng.o
spng.o: spng.c spng.h
fpng.o: fpng.cpp fpng.h
.PHONY: clean
clean:
	rm *.o mud
