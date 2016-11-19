INCFLAGS        += -I/opt/local/include -I/opt/local/include/freetype2
CXXFLAGS        += $(INCFLAGS) -g -Wall --std=c++11 -O
LDFLAGS         += -L/opt/local/lib
LDLIBS          += -lglfw -lfreeimageplus -lfreetype -framework OpenGL -framework Cocoa -framework IOkit

OBJECTS         = apple2e.o keyboard.o dis6502.o fake6502.o interface.o

all: apple2e

apple2e: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm $(OBJECTS)
