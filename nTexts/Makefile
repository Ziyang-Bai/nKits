DEBUG ?= FALSE
VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing

GCC = nspire-gcc
LD = nspire-ld
GENZEHN = genzehn
HOST_CC ?= cc
HOST_CFLAGS ?= -Wall -Wextra -std=c99

CPPFLAGS = -DNKITS_VERSION='"$(VERSION)"' -DNKITS_COMMIT='"$(COMMIT)"' \
	-DNKITS_STAGE='"$(STAGE)"'
GCCFLAGS = -Wall -Wextra -marm -std=c99 -MMD -MP -I. \
	-ffunction-sections -fdata-sections
LDFLAGS = -Wl,--nspireio,--gc-sections
ZEHNFLAGS = --name "nTexts" --240x320-support true --uses-lcd-blit true

ifeq ($(DEBUG),TRUE)
GCCFLAGS += -O0 -g
else
GCCFLAGS += -Os
endif

EXE = nTexts
SOURCES = main.c app.c interface.c library.c reader.c settings.c app_language.c app_log.c \
	text_engine.c reader_index.c storage.c crc32.c chapter_rules.c file_replace.c
OBJS = $(addprefix .build/,$(SOURCES:.c=.o)) .build/common/draw.o
DEPS = $(OBJS:.o=.d)
HOST_TEST = .build/tests/test_host
HOST_TEST_SOURCES = tests/test_host.c tests/host_fakes.c crc32.c chapter_rules.c file_replace.c text_engine.c reader_index.c storage.c
HOST_TEST_HEADERS = app_log.h chapter_rules.h crc32.h file_replace.h gb18030_table.h reader_index.h storage.h text_engine.h \
	tests/fakes/libndls.h tests/fakes/ngc.h

all: $(EXE).tns

.build/%.o: %.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(GCCFLAGS) -c $< -o $@

.build/common/draw.o: ../common/draw.c ../common/draw.h
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(GCCFLAGS) -c $< -o $@

.build/interface.o: ../version.h FORCE

.build/$(EXE).elf: $(OBJS)
	$(LD) $^ -o $@ $(LDFLAGS)

$(EXE).tns: .build/$(EXE).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	rm -f $@.zehn

$(HOST_TEST): $(HOST_TEST_SOURCES) $(HOST_TEST_HEADERS)
	mkdir -p .build/tests
	$(HOST_CC) $(HOST_CFLAGS) -Itests/fakes -I. $(HOST_TEST_SOURCES) -o $@

clean:
	rm -rf .build
	rm -f $(EXE).tns $(EXE).tns.zehn

host-test: $(HOST_TEST)
	rm -rf .build/host-test-data
	mkdir -p .build/host-test-data
	./$(HOST_TEST) .build/host-test-data

test: host-test

-include $(DEPS)

FORCE:

.PHONY: all clean test host-test FORCE
