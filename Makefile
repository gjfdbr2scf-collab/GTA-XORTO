TARGET = game

OBJS = src/main.o src/game.o

CFLAGS = -O2 -G0 -Wall -Iinclude

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
