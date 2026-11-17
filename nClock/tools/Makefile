DEBUG ?= FALSE
VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing
MARKER_CACHE ?= TRUE

GCC := nspire-gcc
LD := nspire-ld
GENZEHN := genzehn
MAKE_PRG := make-prg
PYTHON ?= python3

TARGET := nclock-diag
BUILD := ../build/diagnostic
ifeq ($(MARKER_CACHE),FALSE)
TARGET := nclock-diag-nocache
BUILD := ../build/diagnostic-nocache
endif
SOURCES := src/main.c src/app.c src/clock.c src/ui.c src/power.c \
	tools/diagnose.c tools/diagnose_calls.c
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o)) $(BUILD)/common/resident.o \
	$(BUILD)/common/resident_gate.o $(BUILD)/common/explorer.o $(BUILD)/common/explorer_gate.o \
	$(BUILD)/common/ui.o $(BUILD)/common/draw.o
CPPFLAGS := -DNCLOCK_DIAGNOSTIC -DNKITS_VERSION='"$(VERSION)"' \
	-DNKITS_COMMIT='"$(COMMIT)"' -DNKITS_STAGE='"$(STAGE)"'
ifeq ($(MARKER_CACHE),FALSE)
CPPFLAGS += -DNCLOCK_DIAGNOSTIC_MARKER_CACHE=0
endif
CFLAGS := -std=gnu99 -Wall -Wextra -marm -ffunction-sections -fdata-sections \
	-I../src -I../.. -I../../common -I$(BUILD)
LDFLAGS := -Wl,--gc-sections,--wrap=msleep,--wrap=wait_no_key_pressed,--wrap=lcd_blit
ZEHNFLAGS := --name "nClock diagnostics" --240x320-support true --uses-lcd-blit true

ifeq ($(DEBUG),TRUE)
CFLAGS += -O0 -g
else
CFLAGS += -Os
endif

.PHONY: all clean FORCE
all: $(TARGET).tns

$(BUILD)/resident_build.h: ../../common/build_id.py FORCE
	$(PYTHON) ../../common/build_id.py --output $@ --cc '$(GCC)' \
		--flags '$(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(ZEHNFLAGS)' \
		Makefile ../Makefile $(wildcard ../src/*.c ../src/*.h) diagnose.c diagnose_calls.c \
		../../common/resident.c ../../common/resident.h ../../common/resident_gate.S \
		../../common/explorer.c ../../common/explorer.h ../../common/explorer_gate.S \
		../../common/ui.c ../../common/ui.h ../../common/draw.c ../../common/draw.h ../../version.h

$(BUILD)/%.o: ../%.c $(BUILD)/resident_build.h
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/common/%.o: ../../common/%.c $(BUILD)/resident_build.h
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/common/%.o: ../../common/%.S $(BUILD)/resident_build.h
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(TARGET).tns: $(TARGET).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	$(MAKE_PRG) $@.zehn $@
	rm -f $@.zehn

clean:
	rm -f nclock-diag.elf nclock-diag.tns nclock-diag.tns.zehn \
		nclock-diag-nocache.elf nclock-diag-nocache.tns nclock-diag-nocache.tns.zehn
	rm -rf ../build/diagnostic ../build/diagnostic-nocache

-include $(OBJECTS:.o=.d)
