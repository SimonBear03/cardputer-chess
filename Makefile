CXX ?= g++
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
SOURCES := src/chess.cpp src/coach.cpp src/engine.cpp tests/test_main.cpp
TEST_BINARY := build/cardputer_chess_tests
BENCH_BINARY := build/cardputer_chess_benchmark
UCI_BINARY := build/cardputer_chess_uci

.PHONY: all test test-sanitize benchmark uci clean

all: test

$(TEST_BINARY): $(SOURCES) include/cardputer_chess/chess.hpp include/cardputer_chess/engine.hpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $(TEST_BINARY)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

$(BENCH_BINARY): src/chess.cpp src/engine.cpp tools/benchmark.cpp \
		include/cardputer_chess/chess.hpp include/cardputer_chess/engine.hpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/chess.cpp src/engine.cpp tools/benchmark.cpp \
		-o $(BENCH_BINARY)

benchmark: $(BENCH_BINARY)
	./$(BENCH_BINARY)

$(UCI_BINARY): src/chess.cpp src/engine.cpp tools/uci_main.cpp \
		include/cardputer_chess/chess.hpp include/cardputer_chess/engine.hpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/chess.cpp src/engine.cpp tools/uci_main.cpp \
		-o $(UCI_BINARY)

uci: $(UCI_BINARY)

test-sanitize:
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(SOURCES) -o build/cardputer_chess_tests_sanitize
	# LeakSanitizer requires ptrace support that sandboxed runners may deny.
	# Address and undefined-behavior instrumentation still cover the full suite.
	ASAN_OPTIONS=detect_leaks=0 ./build/cardputer_chess_tests_sanitize

clean:
	$(RM) -r build
