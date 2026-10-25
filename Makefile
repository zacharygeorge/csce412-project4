CXX = g++
CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic

ifeq ($(OS),Windows_NT)
TARGET := load_balancer.exe
CLEAN_COMMAND = powershell -NoProfile -Command "$$files = @('src/main.o','src/Config.o','src/Request.o','src/WebServer.o','src/LoadBalancer.o','src/main.d','src/Config.d','src/Request.d','src/WebServer.d','src/LoadBalancer.d','load_balancer','load_balancer.exe'); foreach ($$file in $$files) { if (Test-Path -LiteralPath $$file) { Remove-Item -Force -LiteralPath $$file } }"
else
TARGET := load_balancer
CLEAN_COMMAND = rm -f $(OBJECTS) $(DEPENDENCIES) load_balancer load_balancer.exe
endif

SOURCES := src/main.cpp \
	src/Config.cpp \
	src/Request.cpp \
	src/WebServer.cpp \
	src/LoadBalancer.cpp
OBJECTS := $(SOURCES:.cpp=.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	$(CLEAN_COMMAND)

-include $(DEPENDENCIES)
