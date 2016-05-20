SERIAL_PORT := /dev/cu.usbserial-DA01I67U


all: build

clean:
	platformio run -t clean

build:
	platformio run

install:
	platformio run -t upload --upload-port $(SERIAL_PORT)

monitor:
	platformio serialports monitor --port $(SERIAL_PORT) --baud 115200
