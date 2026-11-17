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

TARGET := nusb
C_SOURCES := src/main.c src/app.c src/browser.c src/input.c src/ui.c \
	src/fs.c src/usb_msc.c src/bot.c src/resident.c src/resident_check.c \
	src/native_fs.c src/native_path.c src/native_namespace.c \
	src/storage.c src/fileops.c src/name_editor.c src/text.c src/language.c \
	third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
ASM_SOURCES := src/resident_gate.S
OBJECTS := $(addprefix build/,$(C_SOURCES:.c=.o) $(ASM_SOURCES:.S=.o)) build/common/draw.o
CPPFLAGS := -DNKITS_VERSION='"$(VERSION)"' -DNKITS_COMMIT='"$(COMMIT)"' \
	-DNKITS_STAGE='"$(STAGE)"'
CFLAGS := -std=gnu99 -Wall -Wextra -marm -ffunction-sections -fdata-sections \
	-Isrc -I.. -Ithird_party/fatfs -Ibuild
ASFLAGS := -marm -Isrc -Ibuild
LDFLAGS := -Wl,--gc-sections
ZEHNFLAGS := --name "nUSB" --240x320-support true --uses-lcd-blit true
HOST_CFLAGS := -std=gnu99 -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -DNUSB_HOST \
	-Wall -Wextra -Itests -Isrc -I.. -Ithird_party/fatfs
FAT_SOURCES := tests/image_disk.c src/fs.c third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
FAT_HEADERS := tests/image_disk.h src/fs.h src/usb_msc.h third_party/fatfs/ff.h \
	third_party/fatfs/ffconf.h third_party/fatfs/diskio.h
STORAGE_SOURCES := src/storage.c
STORAGE_HEADERS := src/storage.h src/resident.h
JOB_SOURCES := src/fileops.c
JOB_HEADERS := src/fileops.h

ifeq ($(DEBUG),TRUE)
CFLAGS += -O0 -g
else
CFLAGS += -Os
endif

.PHONY: all clean host-test large-test FORCE

all: $(TARGET).tns

# Recheck SDK archives and command-line metadata, keeping the header timestamp
# unchanged when the identity is unchanged.
build/resident_build.h: tests/build_id.py Makefile FORCE
	$(PYTHON) tests/build_id.py --output $@ --cc '$(GCC)' --ld '$(LD)' \
		--cppflags '$(subst ','"'"',$(CPPFLAGS))' --cflags '$(CFLAGS)' \
		--asflags '$(ASFLAGS)' --ldflags='$(LDFLAGS)' --zehnflags '$(ZEHNFLAGS)' \
		--version '$(VERSION)' --commit '$(COMMIT)' --stage '$(STAGE)'

$(OBJECTS): build/resident_build.h

build/common/%.o: ../common/%.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/%.o: %.c
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/%.o: %.S
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(TARGET).tns: $(TARGET).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	$(MAKE_PRG) $@.zehn $@
	rm -f $@.zehn

build/fixtures/.stamp: tests/make_images.py
	mkdir -p build/fixtures
	$(PYTHON) tests/make_images.py build/fixtures
	touch $@

build/test_bot: tests/test_bot.c src/bot.c src/bot.h src/usb_msc.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_bot.c src/bot.c -o $@

build/test_resident: tests/test_resident.c src/resident_check.c src/resident_check.h src/resident.h src/fs.h src/usb_msc.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_resident.c src/resident_check.c -o $@

build/test_fs: tests/test_fs.c $(FAT_SOURCES) $(FAT_HEADERS)
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_fs.c $(FAT_SOURCES) -o $@

build/test_browser: tests/test_browser.c src/browser.c src/browser.h $(STORAGE_SOURCES) $(STORAGE_HEADERS) $(FAT_SOURCES) $(FAT_HEADERS)
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_browser.c src/browser.c $(STORAGE_SOURCES) $(FAT_SOURCES) -o $@

build/test_app: tests/test_app.c src/app.c src/app.h src/input.h src/resident.h src/browser.c src/browser.h src/name_editor.c src/name_editor.h src/text.c src/text.h src/language.c src/language.h $(JOB_SOURCES) $(JOB_HEADERS) $(STORAGE_SOURCES) $(STORAGE_HEADERS) $(FAT_SOURCES) $(FAT_HEADERS)
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_app.c src/app.c src/browser.c src/name_editor.c src/text.c src/language.c $(JOB_SOURCES) $(STORAGE_SOURCES) $(FAT_SOURCES) -o $@

build/test_input: tests/test_input.c tests/libndls.h src/input.c src/input.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_input.c src/input.c -o $@

build/test_native_path: tests/test_native_path.c src/native_path.c src/native_path.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_native_path.c src/native_path.c -o $@

build/test_native_namespace: tests/test_native_namespace.c src/native_namespace.c src/native_namespace.h src/native_path.c src/native_path.h $(FAT_SOURCES) $(FAT_HEADERS)
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_native_namespace.c src/native_namespace.c src/native_path.c $(FAT_SOURCES) -o $@

build/test_fileops: tests/test_fileops.c $(JOB_SOURCES) $(JOB_HEADERS) $(STORAGE_SOURCES) $(STORAGE_HEADERS) $(FAT_SOURCES) $(FAT_HEADERS)
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_fileops.c $(JOB_SOURCES) $(STORAGE_SOURCES) $(FAT_SOURCES) \
		-Wl,--wrap=fdopen,--wrap=fseeko64,--wrap=ftello64,--wrap=unlink -o $@

build/test_name_editor: tests/test_name_editor.c src/name_editor.c src/name_editor.h src/input.h src/text.c src/text.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_name_editor.c src/name_editor.c src/text.c -o $@

build/test_text: tests/test_text.c src/text.c src/text.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_text.c src/text.c -o $@

build/test_language: tests/test_language.c src/language.c src/language.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_language.c src/language.c -o $@

host-test: build/fixtures/.stamp build/test_bot build/test_resident build/test_fs build/test_browser build/test_app build/test_input build/test_native_path build/test_native_namespace build/test_fileops build/test_name_editor build/test_text build/test_language
	./build/test_bot
	./build/test_resident
	./build/test_fs build/fixtures
	./build/test_browser build/fixtures
	./build/test_app build/fixtures
	./build/test_input
	./build/test_native_path
	./build/test_native_namespace build/fixtures
	./build/test_fileops build/fixtures
	./build/test_name_editor
	./build/test_text
	./build/test_language
	$(PYTHON) tests/test_sparse_copy.py

build/test_large_fs: tests/test_large_fs.c $(JOB_SOURCES) $(JOB_HEADERS) $(STORAGE_SOURCES) $(STORAGE_HEADERS) $(FAT_SOURCES) $(FAT_HEADERS)
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) tests/test_large_fs.c $(JOB_SOURCES) $(STORAGE_SOURCES) $(FAT_SOURCES) -o $@

large-test: build/test_large_fs
	$(PYTHON) tests/run_large_tests.py --workdir "$(LARGE_WORKDIR)" \
		--mkfs-exfat "$(MKFS_EXFAT)" --fsck-exfat "$(FSCK_EXFAT)" \
		--driver "$(abspath build/test_large_fs)"

clean:
	rm -f $(TARGET).elf $(TARGET).tns $(TARGET).tns.zehn
	rm -rf build

-include $(OBJECTS:.o=.d)
