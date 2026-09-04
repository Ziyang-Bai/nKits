.PHONY: all test clean

all:
	$(MAKE) -C nHex
	$(MAKE) -C nEdit

test:
	$(MAKE) -C nHex host-test
	$(MAKE) -C nEdit host-test

clean:
	$(MAKE) -C nHex clean
	$(MAKE) -C nEdit clean
