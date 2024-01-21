INCLUDE     +=
CFLAGS      += -g -Wall -O2 $(INCLUDE)
LIBS        +=
LDFLAGS     :=
CXXFLAGS    := $(CFLAGS)
SOURCE      := $(wildcard *.c) $(wildcard *.cpp)
OBJS        := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))
TARGET      := fpga_gpio

CC = /usr/bin/arm-linux-gnueabihf-gcc

.PHONY : everything objs clean distclean rebuild

all : $(TARGET)

objs : $(OBJS)

rebuild: distclean all

clean :
	rm -rf *~
	rm -rf *.o

distclean : clean
	rm -rf $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)
