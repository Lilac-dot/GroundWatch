# GroundWatch 8051 firmware - build with SDCC
#   make         -> build/groundwatch.hex  (flash this)
#   make test    -> runs the fusion-table and detector tests on the Mac
#   make replay  -> build/replay: run a raw recording through the detector
#   make clean

SDCC_BIN ?= $(HOME)/.local/sdcc/bin
CC       = $(SDCC_BIN)/sdcc
PACKIHX  = $(SDCC_BIN)/packihx
CFLAGS   = -mmcs51 --model-small --iram-size 256 --code-size 8192 --std-sdcc11 -Isrc
SRCS     = main.c sense.c adc0808.c fusion.c uart.c lcd.c
HDRS     = $(wildcard src/*.h)

all: build/groundwatch.hex

build:
	mkdir -p build

build/%.rel: src/%.c $(HDRS) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/groundwatch.ihx: $(SRCS:%.c=build/%.rel)
	$(CC) $(CFLAGS) $^ -o $@

build/%.hex: build/%.ihx
	$(PACKIHX) $< > $@
	@grep -E "ROM/EPROM/FLASH|Stack starts" build/groundwatch.mem || true

# the default macOS 27 SDK doesn't link with these Command Line Tools; 15 does
HOST_SDK = $(if $(wildcard /Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk),-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk)

test: build
	cc $(HOST_SDK) -Wall -Wextra -Isrc tests/test_fusion.c src/fusion.c -o build/test_fusion
	./build/test_fusion
	cc $(HOST_SDK) -Wall -Wextra -Isrc -Itests -include tests/host_8051.h \
	   tests/test_sense.c tests/sense_sim.c tests/host_8051.c src/sense.c src/adc0808.c src/fusion.c \
	   -lm -o build/test_sense
	./build/test_sense

# replay a raw recording through the firmware detector: ./build/replay file.csv
replay: build
	cc $(HOST_SDK) -Wall -Wextra -Isrc -Itests -include tests/host_8051.h \
	   tests/replay.c tests/sense_sim.c tests/host_8051.c src/sense.c src/adc0808.c src/fusion.c \
	   -o build/replay

clean:
	rm -rf build

.PHONY: all test clean replay
