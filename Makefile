INCFLAGS        += -I/opt/local/include
CXXFLAGS        += $(INCFLAGS) -g -Wall --std=c++11 -O2
LDFLAGS         += -L/opt/local/lib
LDLIBS          += -lglfw -lao -framework OpenGL -framework Cocoa -framework IOkit

OBJECTS         = apple2e.o dis6502.o fake6502.o interface.o

# keyboard.o

all: apple2e

apple2e: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm $(OBJECTS)
