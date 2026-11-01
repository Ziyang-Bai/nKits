VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing

.PHONY: all test clean

all:
	$(MAKE) -C nHex VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nEdit VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nUSB VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nUSB/tools VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nClock VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nClock/tools VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nCapture VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nProtect VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nHide VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C ThemeEditor VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C TaskManager VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"

test:
	$(MAKE) -C nHex host-test
	$(MAKE) -C nEdit host-test
	$(MAKE) -C nUSB host-test
	$(MAKE) -C nUSB/tools host-test
	$(MAKE) -C nClock host-test
	$(MAKE) -C nCapture host-test
	$(MAKE) -C nProtect host-test
	$(MAKE) -C ThemeEditor host-test
	$(MAKE) -C TaskManager host-test

clean:
	$(MAKE) -C nHex clean
	$(MAKE) -C nEdit clean
	$(MAKE) -C nUSB/tools clean
	$(MAKE) -C nUSB clean
	$(MAKE) -C nClock clean
	$(MAKE) -C nCapture clean
	$(MAKE) -C nProtect clean
	$(MAKE) -C nHide clean
	$(MAKE) -C ThemeEditor clean
	$(MAKE) -C TaskManager clean
