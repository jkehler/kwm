# wwm - wayland window manager
# See LICENSE file for copyright and license details

SRC = kwm.c server.c
OBJ = ${SRC:.c=.o}
CFLAGS = -DWLR_USE_UNSTABLE \
	$(shell pkg-config --cflags --libs wlroots) \
	$(shell pkg-config --cflags --libs wayland-server) \
	$(shell pkg-config --cflags --libs xkbcommon) \
	-I.

WAYLAND_PROTOCOLS=/usr/share/wayland-protocols

all: options kwm

options:
	@echo kwm build options:
	@echo "CFLAGS	= ${CFLAGS}"
	@echo "LDFLAGS	= ${LDFLAGS}"
	@echo "CC		= ${CC}"

xdg-shell-protocol.h:
	wayland-scanner server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c:
	wayland-scanner private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: xdg-shell-protocol.h xdg-shell-protocol.c

kwm: ${OBJ}
	${CC} -o $@ ${OBJ} ${CFLAGS} ${LDFLAGS}

clean:
	rm -f kwm ${OBJ}

.PHONY: all options
