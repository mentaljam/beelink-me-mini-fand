CC ?= cc
CFLAGS ?= -O2
PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin
SYSTEMD_UNIT_DIR ?= /etc/systemd/system

program = beelink-me-mini-fand
build_dir = build
binary = $(build_dir)/$(program)
objects = $(addprefix $(build_dir)/,main.o hwmon.o curve.o)

.PHONY: all clean install install-strip

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

clean:
	$(RM) $(binary) $(objects)
