CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread
TARGET = async_server
SOURCES = main.cpp server.cpp
OBJECTS = $(SOURCES:.cpp=.o)

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)

install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/
	install -d /usr/local/lib/systemd/system
	install -m 644 async-server.service /usr/local/lib/systemd/system/
	systemctl daemon-reload

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /usr/local/lib/systemd/system/async-server.service
	systemctl daemon-reload