FQBN ?= esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc
PORT ?= /dev/cu.usbmodem1101
BAUD ?= 115200

.PHONY: compile-capture upload-capture compile-replay upload-replay compile-control upload-control monitor list-boards list-ports

compile-capture:
	arduino-cli compile --fqbn $(FQBN) capture_ir

upload-capture:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) capture_ir

compile-replay:
	arduino-cli compile --fqbn $(FQBN) replay_kelon168

upload-replay:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) replay_kelon168

compile-control:
	arduino-cli compile --fqbn $(FQBN) control_kelon168

upload-control:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) control_kelon168

monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

list-boards:
	arduino-cli board listall esp32

list-ports:
	arduino-cli board list
