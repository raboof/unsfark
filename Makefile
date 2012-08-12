all: unsfark

.PHONY: clean

clean:
	-rm unsfark

unsfark: unsfark.c
	gcc -Wall -g -mfpmath=387 -o unsfark unsfark.c `pkg-config --cflags --libs gtk+-3.0` -lpthread -export-dynamic
