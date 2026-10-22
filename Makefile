# Canon MX870 driver for macOS (arm64).
#
#   make            build libmx870, the CUPS filter and the PPD
#   make check      unit tests + independent stream validation (tools/bjdump.py)
#   make e2e        PDF -> macOS cupsfilter -> rastertomx870 -> bjdump, no printer needed
#   sudo make install
#   make modes      regenerate tables from your copy of the Windows driver

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration
ARCHFLAGS ?= -arch arm64
BUILD   := build

LIB_SRC := src/libmx870/encoder.c src/libmx870/codec.c src/libmx870/color.c src/libmx870/tables.c src/libmx870/sheet.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)
LIB     := $(BUILD)/libmx870.a

WINDRV  := vendor/mx870-1.06/cab/PRNG3/cnm_0367.dll

PREFIX  := /Library/Printers/Canon-MX870
FILTER  := $(PREFIX)/Filter/rastertomx870
PPD     := $(BUILD)/canon-mx870.ppd

.PHONY: all app app-test check e2e net install uninstall clean modes

all: $(LIB) $(BUILD)/rastertomx870 $(PPD) $(BUILD)/mx870-bjnp

$(BUILD)/libbjnp.o: src/libbjnp/bjnp.c src/libbjnp/bjnp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DARWIN_C_SOURCE $(ARCHFLAGS) -c $< -o $@

$(BUILD)/mx870-bjnp: src/tools/mx870-bjnp.c $(BUILD)/libbjnp.o
	$(CC) $(CFLAGS) -D_DARWIN_C_SOURCE $(ARCHFLAGS) $< $(BUILD)/libbjnp.o -o $@

# PAPPL printer application (needs PAPPL built into vendor/pappl-install, see docs).
PAPPL_PC := PKG_CONFIG_PATH="$(CURDIR)/vendor/pappl-install/lib/pkgconfig:$$(brew --prefix openssl@3)/lib/pkgconfig:$$(brew --prefix jpeg-turbo)/lib/pkgconfig:$$(brew --prefix libpng)/lib/pkgconfig:$$(brew --prefix libusb)/lib/pkgconfig" pkg-config --static
app: $(BUILD)/mx870-app

$(BUILD)/mx870-app: src/app/mx870-app.c $(LIB) $(BUILD)/libbjnp.o
	$(CC) $(CFLAGS) -D_DARWIN_C_SOURCE -Wno-deprecated-declarations $(ARCHFLAGS) $$($(PAPPL_PC) --cflags pappl) \
	  $< $(LIB) $(BUILD)/libbjnp.o $$($(PAPPL_PC) --libs pappl) -o $@

app-test: app check
	sh tests/app.sh $(BUILD) $(BUILD)/samples/a4_preview.png

net: all check
	sh tests/net.sh $(BUILD)

$(PPD): tools/gen_ppd.py src/libmx870/tables.c src/libmx870/papers.inc
	@mkdir -p $(BUILD)
	python3 tools/gen_ppd.py $(FILTER) > $@

$(BUILD)/%.o: src/%.c src/libmx870/mx870.h src/libmx870/mx870_private.h \
              src/libmx870/modes.inc src/libmx870/papers.inc
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ARCHFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

$(BUILD)/rastertomx870: src/cups/rastertomx870.c $(LIB)
	$(CC) $(CFLAGS) $(ARCHFLAGS) -Wno-deprecated-declarations $< $(LIB) -lcups -o $@

$(BUILD)/test_mx870: tests/test_mx870.c $(LIB)
	$(CC) $(CFLAGS) $(ARCHFLAGS) $< $(LIB) -o $@

check: $(BUILD)/test_mx870
	@mkdir -p $(BUILD)/samples
	$(BUILD)/test_mx870 $(BUILD)/samples
	@for f in $(BUILD)/samples/*.bin; do \
	  echo "== bjdump $$f"; \
	  rm -rf $(BUILD)/samples/planes; \
	  python3 tools/bjdump.py -q $$f --planes $(BUILD)/samples/planes > /dev/null || exit 1; \
	done
	@rm -rf $(BUILD)/samples/a4 && python3 tools/bjdump.py -q $(BUILD)/samples/sample_a4_plain_std.bin --planes $(BUILD)/samples/a4 > /dev/null
	python3 tools/preview.py $(BUILD)/samples/a4 1 $(BUILD)/samples/a4_preview.ppm 8
	sips -s format png $(BUILD)/samples/a4_preview.ppm --out $(BUILD)/samples/a4_preview.png > /dev/null

e2e: all
	@test -f $(BUILD)/samples/a4_preview.png || $(MAKE) check
	@mkdir -p $(BUILD)/e2e
	sips -s format pdf $(BUILD)/samples/a4_preview.png --out $(BUILD)/e2e/chart.pdf > /dev/null
	sh tests/e2e.sh $(BUILD) $(BUILD)/e2e/chart.pdf

# sudo make install  -- then add the printer with System Settings or lpadmin.
PPDDIR := /Library/Printers/PPDs/Contents/Resources
install: all
	install -d -o root -g wheel -m 755 $(PREFIX)/Filter
	install -o root -g wheel -m 755 $(BUILD)/rastertomx870 $(FILTER)
	install -d -o root -g wheel -m 755 $(PREFIX)/bin
	install -o root -g wheel -m 755 $(BUILD)/mx870-bjnp $(PREFIX)/bin/
	if [ -x $(BUILD)/mx870-app ]; then install -o root -g wheel -m 755 $(BUILD)/mx870-app $(PREFIX)/bin/; fi
	gzip -c $(PPD) > "$(PPDDIR)/Canon MX870 libmx870.ppd.gz"
	chown root:wheel "$(PPDDIR)/Canon MX870 libmx870.ppd.gz"
	chmod 644 "$(PPDDIR)/Canon MX870 libmx870.ppd.gz"

uninstall:
	rm -rf $(PREFIX) "$(PPDDIR)/Canon MX870 libmx870.ppd.gz"

modes: $(WINDRV)
	python3 tools/extract_modes.py $(WINDRV) > $(BUILD)/modes.json
	python3 tools/gen_modes.py $(BUILD)/modes.json > src/libmx870/modes.inc
	python3 tools/gen_papers.py $(WINDRV) > src/libmx870/papers.inc

clean:
	rm -rf $(BUILD)
