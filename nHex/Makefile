DEBUG ?= FALSE
VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing


GCC := nspire-gcc
LD := nspire-ld
GENZEHN := genzehn
MAKE_PRG := make-prg
HOST_CC ?= cc

TARGET := nhex
SOURCES := src/main.c src/app.c src/editor.c src/file_buffer.c src/input.c src/ui.c
OBJECTS := $(SOURCES:.c=.o)
CPPFLAGS := -DNKITS_VERSION='"$(VERSION)"' -DNKITS_COMMIT='"$(COMMIT)"' \
	-DNKITS_STAGE='"$(STAGE)"'

CFLAGS := -std=gnu99 -Wall -Wextra -marm -ffunction-sections -fdata-sections -Isrc -I..
LDFLAGS := -Wl,--gc-sections -lnspireio
ZEHNFLAGS := --name "nHex"

ifeq ($(DEBUG),TRUE)
CFLAGS += -O0 -g
else
CFLAGS += -Os
endif

.PHONY: all clean host-test FORCE

all: $(TARGET).tns
src/ui.o: ../version.h FORCE


src/%.o: src/%.c
	$(GCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(TARGET).tns: $(TARGET).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	$(MAKE_PRG) $@.zehn $@
	rm -f $@.zehn

build/test_core: tests/test_core.c src/editor.c src/file_buffer.c src/editor.h src/file_buffer.h
	mkdir -p build
	$(HOST_CC) -std=c99 -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Werror -Isrc \
		tests/test_core.c src/editor.c src/file_buffer.c -o $@

build/test_input: tests/test_input.c tests/libndls.h src/input.c src/input.h
	mkdir -p build
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Itests -Isrc \
		tests/test_input.c src/input.c -o $@

host-test: build/test_core build/test_input
	./build/test_core
	./build/test_input

clean:
	rm -f $(OBJECTS) $(TARGET).elf $(TARGET).tns $(TARGET).tns.zehn
	rm -rf build
