CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -O2 -Iinclude
PREFIX ?= /usr/local

LIB_SRCS := \
	src/hashing.cpp \
	src/json.cpp \
	src/json_scan.cpp \
	src/schema.cpp \
	src/schema_json.cpp \
	src/value.cpp \
	src/value_access.cpp \
	src/tensor_encode.cpp \
	src/tensor_decode.cpp \
	src/tensor_names.cpp \
	src/tensor_check.cpp \
	src/frozen_encode.cpp \
	src/frozen_write.cpp \
	src/frozen_decode.cpp \
	src/codegen.cpp \
	src/codegen_util.cpp

LIB_OBJS := $(patsubst src/%.cpp,build/%.o,$(LIB_SRCS))

.PHONY: all test bench clean install

all: build/libkiln.a build/gld-kilngen-cpp build/v2_cases_test build/microbench

build/libkiln.a: $(LIB_OBJS)
	ar rcs $@ $^

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I src -c $< -o $@

build/gld-kilngen-cpp: src/kilngen_main.cpp build/libkiln.a
	$(CXX) $(CXXFLAGS) $< -o $@ build/libkiln.a

build/v2_cases_test: tests/v2_cases_test.cpp build/libkiln.a
	$(CXX) $(CXXFLAGS) $< -o $@ build/libkiln.a

build/microbench: benches/microbench.cpp build/libkiln.a
	$(CXX) $(CXXFLAGS) $< -o $@ build/libkiln.a

test: build/v2_cases_test
	./build/v2_cases_test

bench: build/microbench
	./build/microbench

install: build/libkiln.a build/gld-kilngen-cpp
	install -d $(PREFIX)/include/kiln $(PREFIX)/lib $(PREFIX)/bin
	install -m 644 include/kiln/*.hpp $(PREFIX)/include/kiln/
	install -m 644 build/libkiln.a $(PREFIX)/lib/
	install -m 755 build/gld-kilngen-cpp $(PREFIX)/bin/

clean:
	rm -rf build
