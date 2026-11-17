VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing
GCC := nspire-gcc
LD := nspire-ld
GENZEHN := genzehn
MAKE_PRG := make-prg
HOST_CC ?= cc
PYTHON ?= python3

TARGET := nusb-diag
BUILD := ../build/diagnostic
CORE := src/fs.c src/usb_msc.c src/bot.c src/resident.c src/resident_check.c \
	src/native_fs.c src/native_path.c src/native_namespace.c \
	src/input.c src/ui.c src/text.c \
	third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
SOURCES := tools/diagnose_main.c tools/diagnose_run.c tools/diagnose_usb.c $(CORE)
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o)) $(BUILD)/src/resident_gate.o $(BUILD)/common/draw.o
CPPFLAGS := -DNUSB_DIAGNOSTIC=1 -DNKITS_VERSION='"$(VERSION)"' \
	-DNKITS_COMMIT='"$(COMMIT)"' -DNKITS_STAGE='"$(STAGE)"'
CFLAGS := -std=gnu99 -Wall -Wextra -marm -Os -ffunction-sections -fdata-sections \
	-I../src -I../../ -I../third_party/fatfs -I$(BUILD)
ASFLAGS := -marm -I../src -I$(BUILD)
LDFLAGS := -Wl,--gc-sections
ZEHNFLAGS := --name "nUSB diagnostics" --240x320-support true --uses-lcd-blit true
HOST_CFLAGS := -std=gnu99 -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -DNUSB_HOST \
	-Wall -Wextra -I../tests -I../src -I. -I../../ -I../third_party/fatfs
HOST_SOURCES := ../tests/test_diagnose.c diagnose_run.c ../tests/image_disk.c \
	../src/fs.c ../third_party/fatfs/ff.c ../third_party/fatfs/ffunicode.c

.PHONY: all host-test clean FORCE
all: $(TARGET).tns

$(BUILD)/resident_build.h: ../tests/build_id.py Makefile FORCE
	$(PYTHON) ../tests/build_id.py --output $@ --cc '$(GCC)' --ld '$(LD)' \
		--cppflags '$(subst ','"'"',$(CPPFLAGS))' --cflags '$(CFLAGS)' \
		--asflags '$(ASFLAGS)' --ldflags='$(LDFLAGS)' --zehnflags '$(ZEHNFLAGS)' \
		--extra-source tools/diagnose.h --extra-source tools/Makefile \
		$(foreach source,$(filter tools/%,$(SOURCES)),--extra-source $(source)) \
		--version '$(VERSION)' --commit '$(COMMIT)' --stage '$(STAGE)'

$(BUILD)/common/%.o: ../../common/%.c $(BUILD)/resident_build.h
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: ../%.c $(BUILD)/resident_build.h
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: ../%.S $(BUILD)/resident_build.h
	mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(TARGET).tns: $(TARGET).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	$(MAKE_PRG) $@.zehn $@
	rm -f $@.zehn

../build/fixtures/.stamp: ../tests/make_images.py
	$(MAKE) -C .. build/fixtures/.stamp

../build/test_diagnose: $(HOST_SOURCES) diagnose.h ../tests/image_disk.h ../src/fs.h ../src/resident.h ../src/usb_msc.h ../third_party/fatfs/ffconf.h
	mkdir -p ../build
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SOURCES) -o $@

host-test: ../build/fixtures/.stamp ../build/test_diagnose
	../build/test_diagnose ../build/fixtures/fat32-sfd.img
	../build/test_diagnose ../build/fixtures/exfat-sfd.img

clean:
	rm -f $(TARGET).elf $(TARGET).tns $(TARGET).tns.zehn ../build/test_diagnose
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)
