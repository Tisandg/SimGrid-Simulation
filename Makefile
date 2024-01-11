BIN_FILES  =  ejemplo

#cambiar el PATH
INSTALL_PATH = $(HOME)/simgrid-3.32

CC = gcc

CPPFLAGS = -I$(INSTALL_PATH)/include -I/usr/local/include/


NO_PRAYER_FOR_THE_WICKED =	-w -O3 -g   


LDFLAGS = -L$(INSTALL_PATH)/lib/
LDLIBS = -lm -lsimgrid -rdynamic $(INSTALL_PATH)/lib/libsimgrid.so -Wl,-rpath,$(INSTALL_PATH)/lib


all: CFLAGS=$(NO_PRAYER_FOR_THE_WICKED)
all: $(BIN_FILES)
.PHONY : all

ejemplo: ejemplo.o rand.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

clean:
	rm -f $(BIN_FILES) *.o

.SUFFIXES:
.PHONY : clean