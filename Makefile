# SPDX-License-Identifier: MIT
# Ssd1306Oled: an Arduino-free C99 core (ssd1306*.c), precompiled per microcontroller, plus a thin
# Arduino adapter (Ssd1306Oled.cpp) shipped as source.
#
#   make test            host tests (ASan/UBSan) against the fake controller in tests/
#   make dist            core .a for every target
#   make package         test + dist + release packages in build/pkg/
#   make firmware-check  package, then real firmware for every board in FW_BOARDS (arduino-cli)
#   make screens         PNG screenshots of sample screens in build/screens/ (docs: make docs)
#   make font            regenerate src/ssd1306_font5x7.c from tools/font/font5x7.txt
#
# Packages (attached to release v<VERSION> by CI):
#   Ssd1306Oled-<v>-arduino.zip      Arduino library: headers + adapter source + src/<mcu>/libSsd1306Oled.a
#   Ssd1306Oled-<v>-<target>.tar.gz  include/ + lib/libSsd1306Oled.a (pico-sdk, ESP-IDF, CMake, PC tools)
#   SHA256SUMS
#
# Incremental: one source = one object per target, only changed sources are compiled again
# (plus ccache when installed). Objects are named <file>.c.o like Arduino's own: the ESP8266
# linker script puts only code from *.cpp.o / *.c.o into flash, anything else would land in IRAM.

SHELL     = /bin/bash
LIB       = Ssd1306Oled
CORE_SRC  = src/ssd1306.c src/ssd1306_gfx.c src/ssd1306_text.c src/ssd1306_font5x7.c
TEST_SRC  = tests/test_ssd1306.cpp
VERSION  := $(shell sed -n 's/^version=//p' library.properties)
BUILD     = build
PKG       = $(BUILD)/pkg
TAR_DET   = tar --sort=name --mtime='2000-01-01 00:00Z' --owner=0 --group=0 --numeric-owner

# Targets: the folder name of each .a in the Arduino zip is the core's build.mcu.
#   esp8266        xtensa-lx106-elf GCC 10.3 (compiler of Arduino ESP8266 core 3.1.x)
#   esp32          xtensa-esp32-elf GCC 8.4 (core 2.0.17); plain C, links with core 3.x too
#   cortex-m0plus  RP2040 (Pico, Pico W): Arm GNU Toolchain; arduino-pico, Arduino mbed, pico-sdk
#   atmega328p     Uno, Nano, Pro Mini; avr-gcc 7.3 like Arduino AVR core 1.8.x
#   atmega2560     Mega 2560
TARGETS     = linux-x86_64 esp8266 esp32 cortex-m0plus atmega328p atmega2560
MCU_TARGETS = esp8266 esp32 cortex-m0plus atmega328p atmega2560

CC_linux-x86_64  = gcc
AR_linux-x86_64  = gcc-ar
FL_linux-x86_64  = -O2 -fPIC
CC_esp8266       = xtensa-lx106-elf-gcc
AR_esp8266       = xtensa-lx106-elf-gcc-ar
FL_esp8266       = -Os -mlongcalls -mtext-section-literals -falign-functions=4
CC_esp32         = xtensa-esp32-elf-gcc
AR_esp32         = xtensa-esp32-elf-gcc-ar
FL_esp32         = -Os -mlongcalls -fstrict-volatile-bitfields
CC_cortex-m0plus = arm-none-eabi-gcc
AR_cortex-m0plus = arm-none-eabi-gcc-ar
FL_cortex-m0plus = -Os -mcpu=cortex-m0plus -mthumb
CC_atmega328p    = avr-gcc
AR_atmega328p    = avr-gcc-ar
FL_atmega328p    = -Os -mmcu=atmega328p
CC_atmega2560    = avr-gcc
AR_atmega2560    = avr-gcc-ar
FL_atmega2560    = -Os -mmcu=atmega2560

WARN     = -Wall -Wextra -Werror
COMMON   = -std=gnu99 -ffunction-sections -fdata-sections -Isrc $(WARN) $(EXTRA_CFLAGS)
CCACHE  ?= $(shell command -v ccache 2>/dev/null)

HEADERS  = $(wildcard src/*.h)
ADAPTER  = $(filter-out $(CORE_SRC),$(wildcard src/*.cpp src/*.c))
EXTRA_PKG = LICENSE

# arduino-cli boards for firmware-check (FQBN), and the boards the examples are built for
FW_BOARDS = arduino:avr:uno arduino:avr:mega esp8266:esp8266:d1_mini esp32:esp32:esp32 rp2040:rp2040:rpipicow
EX_BOARDS = arduino:avr:uno rp2040:rp2040:rpipicow

.PHONY: all test dist package version-check print-version firmware-check screens docs font clean
all: test

define TARGET_RULES
$(BUILD)/$(1)/%.c.o: src/%.c $(HEADERS) Makefile
	@mkdir -p $$(@D)
	$(CCACHE) $(CC_$(1)) $(FL_$(1)) $(COMMON) -c $$< -o $$@
$(BUILD)/$(1)/lib$(LIB).a: $(patsubst src/%,$(BUILD)/$(1)/%.o,$(CORE_SRC))
	rm -f $$@ && $(AR_$(1)) rcsD $$@ $$^
endef
$(foreach t,$(TARGETS),$(eval $(call TARGET_RULES,$(t))))

dist: $(foreach t,$(TARGETS),$(BUILD)/$(t)/lib$(LIB).a)

# Host tests link the host archive like any user of the package would.
test: $(BUILD)/linux-x86_64/lib$(LIB).a
	@set -e; for t in $(TEST_SRC); do \
	  b=$(BUILD)/$$(basename $$t .cpp); \
	  g++ -std=gnu++17 -O1 -g $(WARN) -fsanitize=address,undefined -fno-sanitize-recover=all \
	    -Isrc -Itests $$t $(BUILD)/linux-x86_64/lib$(LIB).a -o $$b && ./$$b; \
	done

version-check:
	@test -n "$(VERSION)" || { echo "library.properties: no version"; exit 1; }
	@grep -q '^## $(VERSION)' CHANGELOG.md || { echo "CHANGELOG.md: no entry for $(VERSION)"; exit 1; }
	@grep -q '#define SSD1306_VERSION "$(VERSION)"' src/ssd1306.h || { echo "src/ssd1306.h: SSD1306_VERSION is not $(VERSION)"; exit 1; }
	@echo "$(LIB) $(VERSION)"

print-version:
	@echo $(VERSION)

package: test dist version-check
	rm -rf $(PKG) $(BUILD)/stage && mkdir -p $(PKG)
	@# Arduino library: precompiled core per build.mcu, adapter and headers as source
	d=$(BUILD)/stage/arduino/$(LIB); mkdir -p $$d/src; \
	  cp $(HEADERS) $(ADAPTER) $$d/src/; \
	  for t in $(MCU_TARGETS); do mkdir -p $$d/src/$$t; cp $(BUILD)/$$t/lib$(LIB).a $$d/src/$$t/; done; \
	  grep -v '^\(precompiled\|ldflags\)=' library.properties > $$d/library.properties; \
	  printf 'precompiled=true\nldflags=-l$(LIB)\n' >> $$d/library.properties; \
	  cp README.md CHANGELOG.md $(EXTRA_PKG) $$d/; \
	  cp -r examples $$d/; \
	  cd $(BUILD)/stage/arduino && find $(LIB) -exec touch -d '2000-01-01 00:00Z' {} + && \
	  find $(LIB) | LC_ALL=C sort | zip -qX -@ ../../pkg/$(LIB)-$(VERSION)-arduino.zip
	@# plain archives per target
	set -e; for t in $(TARGETS); do \
	  d=$(BUILD)/stage/$(LIB)-$(VERSION)-$$t; mkdir -p $$d/include $$d/lib; \
	  cp $(filter-out src/$(LIB).h,$(HEADERS)) $$d/include/; cp $(BUILD)/$$t/lib$(LIB).a $$d/lib/; \
	  cp README.md CHANGELOG.md $(EXTRA_PKG) $$d/; \
	  $(TAR_DET) -C $(BUILD)/stage -cf - $(LIB)-$(VERSION)-$$t | gzip -n > $(PKG)/$(LIB)-$(VERSION)-$$t.tar.gz; \
	done
	cd $(PKG) && sha256sum * > SHA256SUMS && cat SHA256SUMS

# Real firmware against the packaged zip, for every board; prints flash/RAM use per board.
firmware-check: package
	rm -rf $(BUILD)/fw && mkdir -p $(BUILD)/fw/libraries
	cd $(BUILD)/fw/libraries && unzip -q ../../pkg/$(LIB)-$(VERSION)-arduino.zip
	@set -eo pipefail; for b in $(FW_BOARDS); do \
	  echo "== FirmwareCheck on $$b"; \
	  arduino-cli compile -b $$b --warnings default --libraries $(BUILD)/fw/libraries \
	    --build-path $(BUILD)/fw/out-$$(echo $$b | tr ':' '-') tests/firmware/FirmwareCheck | tail -3; \
	done
	@set -eo pipefail; for b in $(EX_BOARDS); do for e in examples/*/; do \
	  echo "== $$(basename $$e) on $$b"; \
	  arduino-cli compile -b $$b --libraries $(BUILD)/fw/libraries \
	    --build-path $(BUILD)/fw/ex-$$(basename $$e)-$$(echo $$b | tr ':' '-') $$e | tail -2; \
	done; done

# Sample screens drawn through the core and shown by the fake controller, as PNG.
screens: $(BUILD)/linux-x86_64/lib$(LIB).a
	@mkdir -p $(BUILD)/screens
	g++ -std=gnu++17 -O1 $(WARN) -Isrc -Itests tools/screens/screens.cpp $< -o $(BUILD)/screens-tool
	$(BUILD)/screens-tool $(BUILD)/screens

docs: screens
	python3 tools/screens/shrink.py $(BUILD)/screens/overview.png docs/screens.png

font:
	python3 tools/font/gen.py

clean:
	rm -rf $(BUILD)
