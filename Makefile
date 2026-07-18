CXX ?= g++
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
SOURCES := src/chess.cpp src/engine.cpp tests/test_main.cpp
TEST_BINARY := build/cardputer_chess_tests

.PHONY: all test test-sanitize clean

all: test

$(TEST_BINARY): $(SOURCES) include/cardputer_chess/chess.hpp include/cardputer_chess/engine.hpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $(TEST_BINARY)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

test-sanitize:
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(SOURCES) -o build/cardputer_chess_tests_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/cardputer_chess_tests_sanitize

clean:
	$(RM) -r build

