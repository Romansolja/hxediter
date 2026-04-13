CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iinclude
TARGET  = hxediter
SRCS    = src/main.c src/display.c src/fileops.c src/undo.c
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
