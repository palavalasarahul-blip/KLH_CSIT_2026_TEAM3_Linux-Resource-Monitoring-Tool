CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra
GTK_FLAGS := $(shell pkg-config --cflags --libs gtk+-3.0)
TARGET := linux-resource-monitor
SOURCES := main.cpp gui.cpp system_monitor.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) system_monitor.h
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(GTK_FLAGS)

clean:
	rm -f $(TARGET)
