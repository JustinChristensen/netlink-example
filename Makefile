PROG := nlex
CFLAGS := -fsanitize=address -Wall -g -Og
LDFLAGS := 

.PHONY: all
all: $(PROG)

$(PROG): main.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf *.o $(PROG)

