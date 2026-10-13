CC ?= cc
BUILD ?= build
CFLAGS ?= -std=c11 -O0 -g -Wall -Wextra -Werror
CPPFLAGS += -I../firmware/Devices -I../firmware/App -I../firmware/Platform -Ifakes
APP = ../firmware/App/app.c ../firmware/App/events.c ../firmware/App/commands.c
DEVICE = ../firmware/Devices/mpu6050.c
RUNTIME = ../firmware/Platform/runtime.c
SUITES = test_mpu test_calibration test_events test_acceptance test_runtime

.PHONY: test clean sanitize
test: $(SUITES:%=$(BUILD)/%)
	@set -e; for suite in $(SUITES); do ./$(BUILD)/$$suite; done

$(BUILD):
	mkdir -p "$@"
$(BUILD)/test_mpu: test_mpu.c $(DEVICE) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -lm -o "$@"
$(BUILD)/test_runtime: test_runtime.c $(APP) $(DEVICE) $(RUNTIME) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -lm -o "$@"
$(BUILD)/%: %.c $(APP) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -lm -o "$@"
sanitize:
	$(MAKE) test BUILD=build/sanitize CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
clean:
	rm -rf build
