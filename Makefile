CXX ?= c++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic
all: native-engine build/libkfnnue.so
native-engine:
	$(MAKE) -C native
build/libkfnnue.so: src/nnue_bridge.cpp nnue/upstream/main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -fPIC -shared $< -o $@
test: all
	.venv/bin/python native/test_native.py
	.venv/bin/python -m unittest discover -s tests -v
clean:
	rm -rf build
.PHONY: all native-engine test clean
