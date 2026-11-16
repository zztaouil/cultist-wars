CXX ?= c++
CXXFLAGS ?= -O3 -std=c++17 -march=native
PYTHON ?= python3

.PHONY: all generate train pack check matches
all: build/bot build/gen build/referee build/teacher build/baseline build/candidate_local

build:
	mkdir -p build

build/gen: engine.cpp | build
	$(CXX) $(CXXFLAGS) -DLOCAL -DNN_H=64 $< -o $@

build/referee: tools/referee.cpp engine.cpp | build
	$(CXX) $(CXXFLAGS) -DLOCAL $< -o $@

build/baseline: baseline.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/bot: bot.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

teacher.cpp: engine.cpp teacher.bin tools/pack.py
	$(PYTHON) tools/pack.py engine.cpp -n teacher.bin -H 64 --keep-local -o $@

build/teacher: teacher.cpp | build
	$(CXX) $(CXXFLAGS) -DLOCAL $< -o $@

candidate_local.cpp: engine.cpp candidate.bin tools/pack.py
	$(PYTHON) tools/pack.py engine.cpp -n candidate.bin -H 64 --keep-local -o $@

build/candidate_local: candidate_local.cpp | build
	$(CXX) $(CXXFLAGS) -DLOCAL $< -o $@

bot.cpp: engine.cpp candidate.bin tools/pack.py
	$(PYTHON) tools/pack.py engine.cpp -n candidate.bin -H 64 -o $@

generate: build/gen
	$(PYTHON) generate.py

train:
	$(PYTHON) train.py

pack: bot.cpp build/bot build/candidate_local

build/test_accumulator: test_accumulator.cpp engine.cpp | build
	$(CXX) -O1 -g -std=c++17 -fsanitize=address,undefined $< -o $@

check: all build/test_accumulator
	CW_NET=candidate.bin ./build/test_accumulator
	$(PYTHON) verify.py
	$(PYTHON) ../../tests/test_protocol.py ./build/bot

matches: all
	./build/referee build/candidate_local build/teacher -g 128 -n 8000 -j 4 -s 441100 -v > match-teacher.jsonl
	./build/referee build/bot build/baseline -g 64 -t 49 -j 2 -s 541100 -v > match-production.jsonl
	$(PYTHON) summarize.py
