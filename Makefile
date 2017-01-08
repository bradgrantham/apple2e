# OPT             = -O2 # -O2 causes an abort in malloc on new MAINboard.
# OPT             = -g4 -s ASSERTIONS=1 -s DEMANGLE_SUPPORT=1
OPT             = -O1

INCFLAGS        +=
CXXFLAGS        += $(INCFLAGS) $(OPT) -Wall -std=c++11 -s FULL_ES2=1 -s USE_GLFW=3
LDFLAGS         +=
LDLIBS          +=

SOURCES         = apple2e.cpp dis6502.cpp interface.cpp

PRELOAD         = --preload-file packaged@/

all: apple2e.js

apple2e.js: $(SOURCES)
	emcc $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS) $(PRELOAD)

clean:
