# Хостові тести:
CC      := gcc
BUILD   := build
CFLAGS  := -std=gnu99 -Wall -Wextra -g -O0 -I. -I../firmware/Platform -I../firmware/App
SRCS    := test_bringup.c fakes/fake_bus.c

.PHONY: run clean coverage
run: $(BUILD)/test_bringup
	./$(BUILD)/test_bringup > $(BUILD)/firmware_output.log

$(BUILD)/test_bringup: $(SRCS) minitest.h fakes/fake_bus.h ../firmware/App/bringup.c | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@

# Покриття: 
MIN_LINES    ?= 80
MIN_BRANCHES ?= 70
coverage: clean
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) --coverage $(SRCS) -o $(BUILD)/test_bringup_cov
	cd $(BUILD) && ./test_bringup_cov > firmware_output.log
	cd $(BUILD) && gcov -b -o . test_bringup_cov-test_bringup.gcda > gcov_report.txt 2>/dev/null
	@sh coverage_gate.sh $(BUILD)/gcov_report.txt $(MIN_LINES) $(MIN_BRANCHES)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
