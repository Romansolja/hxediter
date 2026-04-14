CXX     = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -Iinclude
TARGET  = hxediter
SRCS    = src/main.cpp src/display.cpp src/fileops.cpp src/undo.cpp
OBJS    = $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
