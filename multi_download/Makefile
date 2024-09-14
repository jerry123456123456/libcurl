CXX = g++
CXXFLAGS = -std=c++11 -Wall

TARGET = download 

all: $(TARGET)

$(TARGET): multi_thread_download.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< -pthread -lcurl -g

clean:
	rm -f $(TARGET)