CXXFLAGS        = -g -O -Wall --std=c++11

OBJECTS         = apple2e.o keyboard.o dis6502.o

all: apple2e

apple2e: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm $(OBJECTS)
