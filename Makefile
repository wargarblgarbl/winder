# Winder — WINGs file manager
#
# Build:
#   make
#   make CFLAGS='-O2 -Wall'    # override defaults
#
# Install (GNU-style DESTDIR/PREFIX):
#   make install
#   make PREFIX=/usr install
#   make DESTDIR=/tmp/stage PREFIX=/usr install
#   make install-strip
#   make uninstall
#
APP      = winder
VERSION  = 0.1.0

# ---- install paths (override with make PREFIX=... DESTDIR=...) ----
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
DATAROOTDIR ?= $(PREFIX)/share
MANDIR      ?= $(DATAROOTDIR)/man
MAN1DIR     ?= $(MANDIR)/man1
DOCDIR      ?= $(DATAROOTDIR)/doc/$(APP)
APPLICATIONSDIR ?= $(DATAROOTDIR)/applications

# ---- tools ----
CC       ?= gcc
INSTALL  ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 755
INSTALL_DATA    ?= $(INSTALL) -m 644
INSTALL_DIR     ?= $(INSTALL) -d
STRIP    ?= strip
RM       ?= rm -f
PKG_CONFIG ?= pkg-config

# ---- build flags ----
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -I./src -DWINDER_VERSION=\"$(VERSION)\"
LDFLAGS ?=

# Prefer pkg-config when WINGs.pc is present; fall back to known libs.
WINGS_LIBS := $(shell $(PKG_CONFIG) --libs WINGs 2>/dev/null)
ifeq ($(strip $(WINGS_LIBS)),)
  LIBS = -lWINGs -lwraster -lWUtil -lX11 -lXft -lXext -lm
else
  LIBS = $(WINGS_LIBS) -lX11 -lXft -lXext -lm
  CFLAGS += $(shell $(PKG_CONFIG) --cflags WINGs 2>/dev/null)
endif

SRCS = src/main.c src/ui.c src/actions.c src/fsutil.c src/history.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean distclean install install-strip uninstall run \
        help print-paths

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

src/%.o: src/%.c src/winder.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(APP) $(OBJS)

distclean: clean

# ---- install / uninstall ----
# Installs:
#   $(BINDIR)/winder
#   $(MAN1DIR)/winder.1
#   $(APPLICATIONSDIR)/winder.desktop
#   $(DOCDIR)/README.md
install: $(APP)
	$(INSTALL_DIR) $(DESTDIR)$(BINDIR)
	$(INSTALL_DIR) $(DESTDIR)$(MAN1DIR)
	$(INSTALL_DIR) $(DESTDIR)$(APPLICATIONSDIR)
	$(INSTALL_DIR) $(DESTDIR)$(DOCDIR)
	$(INSTALL_PROGRAM) $(APP) $(DESTDIR)$(BINDIR)/$(APP)
	$(INSTALL_DATA) man/winder.1 $(DESTDIR)$(MAN1DIR)/winder.1
	$(INSTALL_DATA) data/winder.desktop $(DESTDIR)$(APPLICATIONSDIR)/winder.desktop
	$(INSTALL_DATA) README.md $(DESTDIR)$(DOCDIR)/README.md
	@echo "Installed $(APP) $(VERSION) under $(DESTDIR)$(PREFIX)"

install-strip: $(APP)
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/$(APP)
	$(RM) $(DESTDIR)$(MAN1DIR)/winder.1
	$(RM) $(DESTDIR)$(APPLICATIONSDIR)/winder.desktop
	$(RM) $(DESTDIR)$(DOCDIR)/README.md
	-rmdir $(DESTDIR)$(DOCDIR) 2>/dev/null || true
	@echo "Removed $(APP) from $(DESTDIR)$(PREFIX)"

run: $(APP)
	./$(APP)

print-paths:
	@echo "PREFIX          = $(PREFIX)"
	@echo "DESTDIR         = $(DESTDIR)"
	@echo "BINDIR          = $(DESTDIR)$(BINDIR)"
	@echo "MAN1DIR         = $(DESTDIR)$(MAN1DIR)"
	@echo "APPLICATIONSDIR = $(DESTDIR)$(APPLICATIONSDIR)"
	@echo "DOCDIR          = $(DESTDIR)$(DOCDIR)"

help:
	@echo "Targets:"
	@echo "  all            build $(APP) (default)"
	@echo "  clean          remove build products"
	@echo "  install        install binary, man page, desktop file, docs"
	@echo "  install-strip  install and strip the binary"
	@echo "  uninstall      remove installed files"
	@echo "  run            build and run ./"$(APP)
	@echo "  print-paths    show install destinations"
	@echo "  help           this message"
	@echo ""
	@echo "Variables: PREFIX DESTDIR BINDIR MANDIR DOCDIR CC CFLAGS"
	@echo "Example:   make PREFIX=/usr DESTDIR=/tmp/root install"
