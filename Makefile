# Canon MX870 driver for macOS (arm64).
#
# Bring your own Canon driver: the print-mode and paper tables are generated
# from YOUR copy of mp68-win-mx870-1_06-ea24.exe, nothing from Canon is stored
# here. Simplest path: ./install.sh path/to/mp68-win-mx870-1_06-ea24.exe
#
#   make WINEXE=path/to/mp68-win-mx870-1_06-ea24.exe   (once; extracts into build/assets)
#   make            build libmx870, the CUPS filter, the PPD and mx870-bjnp
#   make app        PAPPL printer application (builds PAPPL into build/pappl)
#   make check      unit tests + independent stream validation (tools/bjdump.py)
#   make e2e        PDF -> macOS cupsfilter -> rastertomx870 -> bjdump, no printer needed
#   make net        BJNP against tests/fake_bjnp.py
#   make app-test   PAPPL application against tests/fake_bjnp.py
#   sudo make install
#   sudo make uninstall   (also removes the MX870 queue and the BJNP relay)

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration
ARCHFLAGS ?= -arch arm64
BUILD   := build

LIB_SRC := src/libmx870/encoder.c src/libmx870/codec.c src/libmx870/color.c src/libmx870/tables.c src/libmx870/sheet.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)
LIB     := $(BUILD)/libmx870.a

ASSETS  := $(BUILD)/assets
GEN     := $(BUILD)/generated
WINDRV  := $(ASSETS)/cnm_0367.dll
WINEXE  ?=

PREFIX  := /Library/Printers/Canon-MX870
FILTER  := $(PREFIX)/Filter/rastertomx870
PPD     := $(BUILD)/canon-mx870.ppd

.PHONY: all app app-test check e2e net install uninstall clean distclean

all: $(LIB) $(BUILD)/rastertomx870 $(PPD) $(BUILD)/mx870-bjnp

$(BUILD)/libbjnp.o: src/libbjnp/bjnp.c src/libbjnp/bjnp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DARWIN_C_SOURCE $(ARCHFLAGS) -c $< -o $@

$(BUILD)/mx870-bjnp: src/tools/mx870-bjnp.c $(BUILD)/libbjnp.o
	$(CC) $(CFLAGS) -D_DARWIN_C_SOURCE $(ARCHFLAGS) $< $(BUILD)/libbjnp.o -o $@

# Canon model database, extracted from the user's own installer.
$(WINDRV):
	@if [ -z "$(WINEXE)" ]; then \
	  echo "error: Canon driver tables missing. Run ./install.sh path/to/mp68-win-mx870-1_06-ea24.exe"; \
	  echo "       or: make WINEXE=path/to/mp68-win-mx870-1_06-ea24.exe"; exit 1; fi
	sh tools/extract_assets.sh "$(WINEXE)" $(ASSETS)

$(GEN)/modes.inc: $(WINDRV) tools/extract_modes.py tools/gen_modes.py
	@mkdir -p $(GEN)
	python3 tools/extract_modes.py $(WINDRV) > $(GEN)/modes.json
	python3 tools/gen_modes.py $(GEN)/modes.json > $@

$(GEN)/papers.inc: $(WINDRV) tools/gen_papers.py
	@mkdir -p $(GEN)
	python3 tools/gen_papers.py $(WINDRV) > $@

# PAPPL printer application; PAPPL is built from source into build/pappl.
PAPPL    := $(BUILD)/pappl
$(PAPPL)/lib/libpappl.a:
	sh tools/build_pappl.sh $(PAPPL)

PAPPL_PC := PKG_CONFIG_PATH="$(CURDIR)/$(PAPPL)/lib/pkgconfig:$$(brew --prefix openssl@3)/lib/pkgconfig:$$(brew --prefix jpeg-turbo)/lib/pkgconfig:$$(brew --prefix libpng)/lib/pkgconfig:$$(brew --prefix libusb)/lib/pkgconfig" pkg-config --static
app: $(BUILD)/mx870-app

$(BUILD)/mx870-app: src/app/mx870-app.c $(LIB) $(BUILD)/libbjnp.o $(PAPPL)/lib/libpappl.a
	$(CC) $(CFLAGS) -D_DARWIN_C_SOURCE -Wno-deprecated-declarations $(ARCHFLAGS) $$($(PAPPL_PC) --cflags pappl) \
	  $< $(LIB) $(BUILD)/libbjnp.o $$($(PAPPL_PC) --libs pappl) -o $@

app-test: app check
	sh tests/app.sh $(BUILD) $(BUILD)/samples/a4_preview.png

net: all check
	sh tests/net.sh $(BUILD)

$(PPD): tools/gen_ppd.py src/libmx870/tables.c $(GEN)/papers.inc
	@mkdir -p $(BUILD)
	python3 tools/gen_ppd.py $(GEN)/papers.inc $(FILTER) > $@

$(BUILD)/%.o: src/%.c src/libmx870/mx870.h src/libmx870/mx870_private.h \
              $(GEN)/modes.inc $(GEN)/papers.inc
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ARCHFLAGS) -I$(GEN) -c $< -o $@

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

# Also removes what install.sh --printer sets up: the "MX870" queue and the BJNP relay.
RELAY_PLIST := /Library/LaunchDaemons/org.mx870.bjnp-relay.plist
uninstall:
	-lpadmin -x MX870 2> /dev/null
	-launchctl bootout system $(RELAY_PLIST) 2> /dev/null
	rm -f $(RELAY_PLIST) /var/log/mx870-bjnp-relay.log
	rm -rf $(PREFIX) "$(PPDDIR)/Canon MX870 libmx870.ppd.gz"

# Keeps the extracted Canon assets and the PAPPL build.
clean:
	find $(BUILD) -mindepth 1 -maxdepth 1 ! -name assets ! -name 'pappl*' -exec rm -rf {} +

distclean:
	rm -rf $(BUILD)
