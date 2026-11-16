CC ?= cc
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all test test-c test-cpp sanitize sanitize-c clean
all: bot_cpp
bot_cpp: bot.cpp
	$(CXX) $(CXXFLAGS) $< -o $@
bot: bot.c
	$(CC) $(CFLAGS) $< -o $@
tests/test_bot: tests/test_bot.c bot.c
	$(CC) $(CFLAGS) $< -o $@
tests/test_search: tests/test_search.c bot.c
	$(CC) $(CFLAGS) $< -o $@
tests/test_recruitment: tests/test_recruitment.c bot.c
	$(CC) $(CFLAGS) $< -o $@
tests/test_components: tests/test_components.c bot.c
	$(CC) $(CFLAGS) $< -o $@
test: test-cpp
test-c: tests/test_bot tests/test_search tests/test_recruitment tests/test_components bot
	./tests/test_bot
	./tests/test_search
	./tests/test_recruitment
	./tests/test_components
	python3 tests/test_protocol.py
test-cpp: bot_cpp
	python3 tests/test_cpp.py
sanitize: test-cpp
sanitize-c:
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined tests/test_bot.c -o /tmp/cultist-wars-test
	/tmp/cultist-wars-test
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined tests/test_search.c -o /tmp/cultist-wars-search-test
	/tmp/cultist-wars-search-test
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined tests/test_recruitment.c -o /tmp/cultist-wars-recruitment-test
	/tmp/cultist-wars-recruitment-test
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined tests/test_components.c -o /tmp/cultist-wars-components-test
	/tmp/cultist-wars-components-test
clean:
	rm -f bot bot_cpp tests/test_bot tests/test_search tests/test_recruitment tests/test_components
