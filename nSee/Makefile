DEBUG ?= FALSE
VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing

GCC := nspire-gcc
LD := nspire-ld
GENZEHN := genzehn
HOST_CC ?= cc
HOST_CFLAGS ?= -O2 -g -Wall -Wextra -std=c99
PYTHON ?= python3

TARGET := nsee
SOURCES := $(wildcard src/*.c)
JPEG := $(filter-out third_party/libjpeg/jmemansi.c third_party/libjpeg/jmemname.c,$(wildcard third_party/libjpeg/*.c))
PNG := $(wildcard third_party/libpng/*.c)
DECODERS := src/image.c src/readpng.c src/readjpeg.c $(JPEG) $(PNG)
OBJECTS := $(addprefix build/,$(SOURCES:.c=.o) $(JPEG:.c=.o) $(PNG:.c=.o)) \
	build/common/draw.o build/common/ui.o
DEPS := $(OBJECTS:.o=.d)
CPPFLAGS := -DNKITS_VERSION='"$(VERSION)"' -DNKITS_COMMIT='"$(COMMIT)"' \
	-DNKITS_STAGE='"$(STAGE)"'
INCLUDES := -Isrc -I.. -Ithird_party/libpng -Ithird_party/libjpeg
CFLAGS := -std=gnu99 -Wall -Wextra -marm -MMD -MP -ffunction-sections -fdata-sections $(INCLUDES)
LDFLAGS := -Wl,--gc-sections -lz -lm
ZEHNFLAGS := --name "nSee" --240x320-support true --uses-lcd-blit true

ifeq ($(DEBUG),TRUE)
CFLAGS += -O0 -g
else
CFLAGS += -Os
endif

.PHONY: all clean host-test FORCE
all: $(TARGET).tns

build/%.o: %.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/common/%.o: ../common/%.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/src/main.o build/common/ui.o: ../version.h FORCE

build/$(TARGET).elf: $(OBJECTS)
	$(LD) $^ -o $@ $(LDFLAGS)

$(TARGET).tns: build/$(TARGET).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	rm -f $@.zehn

build/test_image: tests/test_image.c $(DECODERS) $(wildcard src/*.h third_party/libpng/*.h third_party/libjpeg/*.h)
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) tests/test_image.c $(DECODERS) -lz -lm -o $@

host-test: build/test_image
	$(PYTHON) tests/fixtures.py build/fixtures
	./build/test_image build/fixtures

clean:
	rm -rf build
	rm -f $(TARGET).tns $(TARGET).tns.zehn nsee-notices.zip

-include $(DEPS)
FORCE:
