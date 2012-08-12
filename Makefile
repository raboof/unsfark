all: unsfark unsfark-gtk

.PHONY: clean

clean:
	-rm unsfark
	-rm unsfark-gtk

unsfark: unsfark.c unsfark.h unsfark-cli.c
	gcc -Wall -g -mfpmath=387 -o unsfark unsfark.c unsfark-cli.c `pkg-config --cflags --libs zlib` 

unsfark-gtk: unsfark.c unsfark.h unsfark-gtk.c
	gcc -Wall -g -mfpmath=387 -o unsfark-gtk unsfark.c unsfark-gtk.c `pkg-config --cflags --libs gtk+-3.0` -lpthread -export-dynamic
