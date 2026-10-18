DEBUG ?= FALSE
VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing

GCC := nspire-gcc
LD := nspire-ld
GENZEHN := genzehn
MAKE_PRG := make-prg
HOST_CC ?= cc
PYTHON ?= python3

TARGET := nclock
SOURCES := src/main.c src/app.c src/clock.c src/ui.c src/power.c
OBJECTS := $(addprefix build/,$(SOURCES:.c=.o)) build/common/resident.o build/common/resident_gate.o
CPPFLAGS := -DNKITS_VERSION='"$(VERSION)"' -DNKITS_COMMIT='"$(COMMIT)"' \
	-DNKITS_STAGE='"$(STAGE)"'
CFLAGS := -std=gnu99 -Wall -Wextra -marm -ffunction-sections -fdata-sections \
	-Isrc -I.. -I../common -Ibuild
LDFLAGS := -Wl,--gc-sections
ZEHNFLAGS := --name "nClock" --240x320-support true --uses-lcd-blit true

ifeq ($(DEBUG),TRUE)
CFLAGS += -O0 -g
else
CFLAGS += -Os
endif

.PHONY: all clean host-test FORCE

all: $(TARGET).tns

build/resident_build.h: ../common/build_id.py FORCE
	$(PYTHON) ../common/build_id.py --output $@ --cc '$(GCC)' \
		--flags '$(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(ZEHNFLAGS)' \
		Makefile $(wildcard src/*.c src/*.h) ../common/resident.c ../common/resident.h \
		../common/resident_gate.S ../version.h

$(OBJECTS): build/resident_build.h

build/src/%.o: src/%.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/common/%.o: ../common/%.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/common/%.o: ../common/%.S
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(TARGET).tns: $(TARGET).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	$(MAKE_PRG) $@.zehn $@
	rm -f $@.zehn

build/test_clock: tests/test_clock.c src/clock.c src/clock.h
	mkdir -p build
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isrc tests/test_clock.c src/clock.c -o $@

host-test: build/test_clock
	./build/test_clock

clean:
	rm -f $(TARGET).elf $(TARGET).tns $(TARGET).tns.zehn
	rm -rf build

-include $(OBJECTS:.o=.d)
