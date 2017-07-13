
TARGET = hashtable_test
CXXFLAGS = -O2 -g -Wall -fmessage-length=0 -std=c++11   
CXX = $(CROSS)g++  $(INCLUDE_DIRS)
LIBS = -lpthread -lrt


APP_OBJS =	./hashtable_test.o    \
	./linux_utils.o              \
	
APP_DEPS = ./hashtable_test.cpp    \
	./linux_utils.cpp              \
	./linux_utils.h              \
	./Makefile              \
	./hashtable.h              \



$(TARGET):: $(APP_DEPS) $(APP_OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) -pthread $(APP_OBJS) $(LIBS)

all: $(TARGET)

clean:
	$(Q)rm -f $(APP_OBJS) $(TARGET) 

	