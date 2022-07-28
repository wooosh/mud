CFLAGS = -lm -lz -O3 -march=native -g
CXXFLAGS = -lm -lz -O3 -march=native -g
mud: mud.cpp spng.o fpng.o
spng.o: spng.c spng.h
fpng.o: fpng.cpp fpng.h
