VERSION ?= dev
COMMIT ?= unknown
STAGE ?= Alpha Testing

.PHONY: all test clean

all:
	$(MAKE) -C nHex VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nEdit VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nClock VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"
	$(MAKE) -C nCapture VERSION="$(VERSION)" COMMIT="$(COMMIT)" STAGE="$(STAGE)"

test:
	$(MAKE) -C nHex host-test
	$(MAKE) -C nEdit host-test
	$(MAKE) -C nClock host-test
	$(MAKE) -C nCapture host-test

clean:
	$(MAKE) -C nHex clean
	$(MAKE) -C nEdit clean
	$(MAKE) -C nClock clean
	$(MAKE) -C nCapture clean
