CXX = g++
CPPOBJS = src/main.o src/gs_uhf.o network/network.o
COBJS = 
CXXFLAGS = -I ./include/ -I ./network/ -Wall -pthread -DGSNID=\"roofuhf\"
EDLDFLAGS := -lsi446x -lpthread -lm
TARGET = roof_uhf.out

all: $(COBJS) $(CPPOBJS)
	$(CXX) $(CXXFLAGS) $(COBJS) $(CPPOBJS) -o $(TARGET) $(EDLDFLAGS)
	sudo ./$(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

%.o: %.c
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	$(RM) *.out
	$(RM) *.o
	$(RM) src/*.o
	$(RM) network/*.o