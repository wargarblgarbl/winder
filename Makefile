# Winder — WINGs file manager
APP      = winder
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin

CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -I./src
LDFLAGS ?=
LIBS     = -lWINGs -lwraster -lWUtil -lX11 -lXft -lXext -lm

SRCS = src/main.c src/ui.c src/actions.c src/fsutil.c src/history.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install uninstall run

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

src/%.o: src/%.c src/winder.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(APP) $(OBJS)

install: $(APP)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(APP) $(DESTDIR)$(BINDIR)/$(APP)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(APP)

run: $(APP)
	./$(APP)
