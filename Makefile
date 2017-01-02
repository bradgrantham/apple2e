INCFLAGS        +=
CXXFLAGS        += $(INCFLAGS) -g -Wall -std=c++11 -O2
LDFLAGS         +=
LDLIBS          +=

SOURCES         = apple2e.cpp dis6502.cpp web_interface.cpp

all: apple2e.js

apple2e.js: $(SOURCES)
	emcc --bind $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS) --preload-file apple2e.rom 

clean:
