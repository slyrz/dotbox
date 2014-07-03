PREFIX?=/usr

DOTBOX_CFLAGS= \
	-Wall \
	-O2

DOTBOX_MACROS= \
	-D_GNU_SOURCE

all: dotbox

dotbox: dotbox.c
	$(CC) $(DOTBOX_CFLAGS) $(DOTBOX_MACROS) -o $@ $< $(DOTBOX_LIBS)

install: dotbox
	install -d "${DESTDIR}${PREFIX}/bin"
	install -t "${DESTDIR}${PREFIX}/bin" -o root -g root -m 4755 $<

clean:
	rm -rf dotbox
