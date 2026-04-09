CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LIBS = -lm

TARGET = ecg_serial_plotter_fb
SOURCE = ecg_serial_plotter_fb.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	rm -f $(TARGET)

install-deps:
	sudo apt-get update
	sudo apt-get install -y build-essential

run: $(TARGET)
	sudo ./$(TARGET)

.PHONY: clean install-deps run