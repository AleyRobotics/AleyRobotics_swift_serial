CFLAGS := -Wall

.PHONY: all
all: ip2ser ip2log

.PHONY: clean
clean:
	rm -f ip2ser ip2log

ip2ser: ip2ser.c
	$(CC) $(CFLAGS) $< -o $@

ip2log: ip2log.c
	$(CC) $(CFLAGS) $< -o $@
