CXXFLAGS=-Wall --std=c++11

all: apple2e

apple2e: apple2e.o keyboard.o
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)
