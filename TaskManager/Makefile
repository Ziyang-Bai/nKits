DEBUG ?= FALSE
VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing

GCC := nspire-gcc
LD := nspire-ld
GENZEHN := genzehn
MAKE_PRG := make-prg
HOST_CC ?= cc

TARGET := taskmanager
SOURCES := src/main.c src/tasks.c src/native.c src/ui.c
OBJECTS := $(addprefix build/,$(SOURCES:.c=.o))
CPPFLAGS := -DNKITS_VERSION='"$(VERSION)"' -DNKITS_COMMIT='"$(COMMIT)"' \
	-DNKITS_STAGE='"$(STAGE)"'
CFLAGS := -std=gnu99 -Wall -Wextra -marm -ffunction-sections -fdata-sections -Isrc -I..
LDFLAGS := -Wl,--gc-sections -lnspireio
ZEHNFLAGS := --name "TaskManager" --240x320-support true --uses-lcd-blit true

ifeq ($(DEBUG),TRUE)
CFLAGS += -O0 -g
else
CFLAGS += -Os
endif

.PHONY: all clean host-test native-test FORCE
all: $(TARGET).tns

build/src/ui.o: ../version.h FORCE

build/src/%.o: src/%.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(TARGET).tns: $(TARGET).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	$(MAKE_PRG) $@.zehn $@
	rm -f $@.zehn

build/test_tasks: tests/test_tasks.c src/tasks.c src/tasks.h
	mkdir -p build
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isrc tests/test_tasks.c src/tasks.c -o $@

host-test: build/test_tasks
	./build/test_tasks

build/test_interrupts.elf: tests/test_interrupts.c src/tasks.h build/src/native.o build/src/tasks.o
	$(GCC) $(CPPFLAGS) $(CFLAGS) -c $< -o build/test_interrupts.o
	$(LD) build/test_interrupts.o build/src/native.o build/src/tasks.o -o $@ -Wl,--gc-sections

build/test_interrupts.tns: build/test_interrupts.elf
	$(GENZEHN) --input $< --output $@.zehn --name "TaskManager IRQ test" --240x320-support true --uses-lcd-blit true
	$(MAKE_PRG) $@.zehn $@
	rm -f $@.zehn

native-test: build/test_interrupts.tns

clean:
	rm -f $(TARGET).elf $(TARGET).tns $(TARGET).tns.zehn
	rm -rf build

-include $(OBJECTS:.o=.d)
