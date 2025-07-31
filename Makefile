CXX = g++

CXXFLAGS = -std=c++17 -Wall -O3 -flto -march=native

TARGET = reconstruction_aman.exe

SRCS = reconstruction.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: all clean