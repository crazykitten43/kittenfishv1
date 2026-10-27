CXX ?= c++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic
all: ../build/kittenfish-cpp
../build/kittenfish-cpp: main.cpp vendor/chess.hpp ../nnue/upstream/main.cpp
	mkdir -p ../build
	$(CXX) $(CXXFLAGS) -pthread main.cpp -o $@
.PHONY: all
