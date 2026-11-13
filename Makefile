CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all test sanitize clean
all: bot
bot: bot.c
	$(CC) $(CFLAGS) $< -o $@
tests/test_bot: tests/test_bot.c bot.c
	$(CC) $(CFLAGS) $< -o $@
test: tests/test_bot bot
	./tests/test_bot
	python3 tests/test_protocol.py
sanitize:
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined tests/test_bot.c -o /tmp/cultist-wars-test
	/tmp/cultist-wars-test
clean:
	rm -f bot tests/test_bot
