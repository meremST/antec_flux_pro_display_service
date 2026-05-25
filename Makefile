CC=gcc
CFLAGS=-O2 -Wall -Wextra
LIBS=-lusb-1.0

TARGET=antec_sensor

all: $(TARGET)

$(TARGET): temp-monitor.c
	$(CC) $(CFLAGS) -o $(TARGET) temp-monitor.c $(LIBS)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)
