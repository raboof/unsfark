all: unsfark

.PHONY: clean

clean:
	-rm unsfark

unsfark: unsfark.c unsfark.h unsfark-gtk.c
	gcc -Wall -g -mfpmath=387 -o unsfark unsfark.c unsfark-gtk.c `pkg-config --cflags --libs gtk+-3.0` -lpthread -export-dynamic
