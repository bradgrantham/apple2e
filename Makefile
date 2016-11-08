CXXFLAGS=-O -Wall --std=c++11

all: apple2e

apple2e: apple2e.o keyboard.o dis6502.o
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)
