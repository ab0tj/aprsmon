CC=g++
CFLAGS=-O2 -Wall -c
OBJS=main.o api.o aprs.o config.o ini.o INIReader.o

all: aprsmon

install: all
	/usr/bin/install -D --mode=755 aprsmon /usr/local/bin/aprsmon

clean:
	rm -f aprsmon $(OBJS)

aprsmon: $(OBJS)
	$(CC) $(OBJS) -o aprsmon

main.o: main.cpp api.h aprs.h config.h
	$(CC) $(CFLAGS) -o main.o main.cpp

api.o: api.cpp api.h
	$(CC) $(CFLAGS) -o api.o api.cpp

aprs.o: aprs.cpp aprs.h
	$(CC) $(CFLAGS) -o aprs.o aprs.cpp

config.o: config.cpp config.h api.h aprs.h INIReader.o
	$(CC) $(CFLAGS) -o config.o config.cpp

ini.o: inih/ini.c inih/ini.h
	gcc $(CFLAGS) -o ini.o inih/ini.c

INIReader.o: inih/cpp/INIReader.cpp inih/cpp/INIReader.h ini.o
	$(CC) $(CFLAGS) -o INIReader.o inih/cpp/INIReader.cpp