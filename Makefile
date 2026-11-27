CC ?= cc
CFLAGS ?= -O2
NFPM ?= nfpm
VERSION ?= 0.0.0
PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin
SYSTEMD_UNIT_DIR ?= /etc/systemd/system

program = beelink-me-mini-fand
build_dir = build
binary = $(build_dir)/$(program)
objects = $(addprefix $(build_dir)/,main.o hwmon.o curve.o)

.PHONY: all clean install install-strip packages

all: $(binary)

$(binary): $(objects)
	$(CC) $(LDFLAGS) -o $@ $(objects) $(LDLIBS)

$(build_dir)/%.o: src/%.c src/fand.h | $(build_dir)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c17 -Wall -Wextra -Wpedantic -c -o $@ $<

$(build_dir):
	mkdir -p $@

install-strip: private strip_flag = -s

install install-strip: $(binary)
	install -Dm755 $(strip_flag) $(binary) "$(DESTDIR)$(SBINDIR)/$(program)"
	sed 's|@SBINDIR@|$(SBINDIR)|g' systemd/$(program).service.in | \
		install -Dm644 /dev/stdin "$(DESTDIR)$(SYSTEMD_UNIT_DIR)/$(program).service"

packages:
	$(MAKE) install-strip DESTDIR="$(CURDIR)/$(build_dir)/pkgroot" PREFIX=/usr \
		SBINDIR=/usr/bin SYSTEMD_UNIT_DIR=/usr/lib/systemd/system
	mkdir -p "$(build_dir)/packages"
	set -e; for format in deb rpm archlinux; do \
		VERSION="$(VERSION)" "$(NFPM)" package --packager "$$format" --target "$(build_dir)/packages/"; \
	done

clean:
	$(RM) $(binary) $(objects)
