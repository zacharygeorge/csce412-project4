CXX = g++
CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic

ifeq ($(OS),Windows_NT)
TARGET := load_balancer.exe
REQUEST_TEST_TARGET := request_webserver_tests.exe
LOAD_BALANCER_TEST_TARGET := load_balancer_tests.exe
RUN_REQUEST_TESTS := request_webserver_tests.exe
RUN_LOAD_BALANCER_TESTS := load_balancer_tests.exe
CLEAN_COMMAND = powershell -NoProfile -Command "$$files = @('src/main.o','src/Config.o','src/Request.o','src/WebServer.o','src/LoadBalancer.o','src/main.d','src/Config.d','src/Request.d','src/WebServer.d','src/LoadBalancer.d','load_balancer','load_balancer.exe','request_webserver_tests','request_webserver_tests.exe','load_balancer_tests','load_balancer_tests.exe'); foreach ($$file in $$files) { if (Test-Path -LiteralPath $$file) { Remove-Item -Force -LiteralPath $$file } }"
else
TARGET := load_balancer
REQUEST_TEST_TARGET := request_webserver_tests
LOAD_BALANCER_TEST_TARGET := load_balancer_tests
RUN_REQUEST_TESTS := ./request_webserver_tests
RUN_LOAD_BALANCER_TESTS := ./load_balancer_tests
CLEAN_COMMAND = rm -f $(OBJECTS) $(DEPENDENCIES) load_balancer load_balancer.exe request_webserver_tests request_webserver_tests.exe load_balancer_tests load_balancer_tests.exe
endif

SOURCES := src/main.cpp \
	src/Config.cpp \
	src/Request.cpp \
	src/WebServer.cpp \
	src/LoadBalancer.cpp
OBJECTS := $(SOURCES:.cpp=.o)
DEPENDENCIES := $(OBJECTS:.o=.d)
TEST_SOURCES := tests/request_webserver_tests.cpp src/Request.cpp src/WebServer.cpp
TEST_HEADERS := include/Request.h include/WebServer.h
LOAD_BALANCER_TEST_SOURCES := tests/load_balancer_tests.cpp \
	src/Config.cpp \
	src/Request.cpp \
	src/WebServer.cpp \
	src/LoadBalancer.cpp
LOAD_BALANCER_TEST_HEADERS := include/Config.h \
	include/NotImplementedError.h \
	include/Request.h \
	include/WebServer.h \
	include/LoadBalancer.h

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(REQUEST_TEST_TARGET): $(TEST_SOURCES) $(TEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_SOURCES) -o $@

$(LOAD_BALANCER_TEST_TARGET): $(LOAD_BALANCER_TEST_SOURCES) $(LOAD_BALANCER_TEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LOAD_BALANCER_TEST_SOURCES) -o $@

test: $(REQUEST_TEST_TARGET) $(LOAD_BALANCER_TEST_TARGET)
	$(RUN_REQUEST_TESTS)
	$(RUN_LOAD_BALANCER_TESTS)

clean:
	$(CLEAN_COMMAND)

-include $(DEPENDENCIES)
