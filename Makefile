IMAGE ?= stm32-build
PORT ?= /dev/ttyACM0

.PHONY: build clean flash monitor

build:
	docker run --rm -v $(PWD):/workspace -w /workspace \
		--user $(shell id -u):$(shell id -g) $(IMAGE) \
		make -C firmware all

clean:
	docker run --rm -v $(PWD):/workspace -w /workspace \
		--user $(shell id -u):$(shell id -g) $(IMAGE) \
		make -C firmware clean

flash:
	st-flash --reset write firmware/build/*.bin 0x08000000

monitor:
	picocom -b 115200 --imap lfcrlf $(PORT)

.PHONY: check-layers
check-layers:
	@bash scripts/check_layers.sh
