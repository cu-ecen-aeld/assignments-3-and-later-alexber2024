CC      := $(CROSS_COMPILE)gcc
CFLAGS  := -Wall -Werror -g

all: aesdsocket

# using implicit rules for building .o files from .c files
aesdsocket: aesdsocket.o

clean:
	rm -f aesdsocket aesdsocket.o
