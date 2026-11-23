CC ?= cc
CFLAGS ?= -O2
PREFIX ?= /usr/local
SYSTEMD_UNIT_DIR ?= /etc/systemd/system

program = beelink-me-mini-fand
build_dir = build
binary = $(build_dir)/$(program)
objects = $(addprefix $(build_dir)/,main.o hwmon.o curve.o)

.PHONY: all clean install

all: $(binary)

$(binary): $(objects)
	$(CC) $(LDFLAGS) -o $@ $(objects) $(LDLIBS)

$(build_dir)/%.o: src/%.c src/fand.h | $(build_dir)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c17 -Wall -Wextra -Wpedantic -c -o $@ $<

$(build_dir):
	mkdir -p $@

install: $(binary)
	install -Dm755 $(binary) "$(DESTDIR)$(PREFIX)/sbin/$(program)"
	sed 's|@SBINDIR@|$(PREFIX)/sbin|g' systemd/$(program).service.in | \
		install -Dm644 /dev/stdin "$(DESTDIR)$(SYSTEMD_UNIT_DIR)/$(program).service"

clean:
	$(RM) $(binary) $(objects)
