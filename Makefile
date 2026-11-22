CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror

.PHONY: all test clean
all: shared_bridge.so

shared_bridge.so: shared_bridge.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< -ldl -pthread

shared_bridge_test.so: shared_bridge.c
	$(CC) $(CFLAGS) -DBRIDGE_TEST -fPIC -shared -o $@ $< -ldl -pthread

mock_lvgl: mock_lvgl.c
	$(CC) $(CFLAGS) -Wl,--export-dynamic -o $@ $< -pthread

mock_libinput.so: mock_libinput.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<

test_hotkey: test_hotkey.c mock_libinput.so
	$(CC) $(CFLAGS) -o $@ test_hotkey.c -L. -l:mock_libinput.so -Wl,-rpath,'$$ORIGIN'

test: shared_bridge_test.so mock_lvgl test_hotkey
	python3 -m unittest -q test_native
	VOICE_TERMINAL_TEST=1 python3 -m unittest -q test_native
	LD_PRELOAD=$(CURDIR)/shared_bridge_test.so ./test_hotkey
	VOICE_DISABLED_TEST=1 LD_PRELOAD=$(CURDIR)/shared_bridge_test.so ./test_hotkey
	python3 test_hotkey_lock.py

clean:
	rm -f shared_bridge.so shared_bridge_test.so mock_lvgl mock_libinput.so test_hotkey
