CC = gcc
CFLAGS = -c -Wall -g -Ofast
LD = $(CC)
LDFLAGS =

TARGET = honkpack
OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
