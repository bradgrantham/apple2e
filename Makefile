INCFLAGS        += -I/opt/local/include
CXXFLAGS        += $(INCFLAGS) -g -Wall --std=c++17 # -O2
LDFLAGS         += -L/opt/local/lib
LDLIBS          += -lglfw -lao -framework OpenGL -framework Cocoa -framework IOkit

OBJECTS         = apple2e.o dis6502.o interface.o gl_utility.o
# fake6502.o

# keyboard.o

all: apple2e

apple2e: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

apple2e.o: cpu6502.h

clean:
	rm $(OBJECTS)
