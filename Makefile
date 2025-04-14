CXX = g++
LDFLAGS = -lsfml-system -lsfml-window -lsfml-graphics -lsfml-audio -lfftw3
SOURCES = main.cpp catmullRom.cpp
TARGET = analyzer

$(TARGET): $(SOURCES)
	$(CXX) $(SOURCES) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)
