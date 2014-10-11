#DEBUG	= -g -O0
DEBUG	= -O3
CC	= gcc
INCLUDE	= -I/usr/local/include
CFLAGS	= $(DEBUG) -Wall $(INCLUDE) -Winline -pipe

LDFLAGS	= -L/usr/local/lib
LDLIBS    = -lwiringPi -lwiringPiDev -lpthread -lm

SRC	=	piglowd.c

OBJ	=	$(SRC:.c=.o)

BINS	=	$(SRC:.c=)

all:	$(BINS)

piglowd:	piglowd.o
	@echo [link]
	@$(CC) -o $@ piglowd.o $(LDFLAGS) $(LDLIBS)

.c.o:
	@echo [CC] $<
	@$(CC) -c $(CFLAGS) $< -o $@

clean:
	@echo "[Clean]"
	@rm -f $(OBJ) *~ core tags $(BINS)

depend:
	makedepend -Y $(SRC)

install:	piglowd
	@echo Installing piglowd into /usr/bin
	@sudo cp piglowd /usr/bin
	@sudo chmod 755 /usr/bin/piglowd
	@sudo cp piglowd.conf /etc/piglowd/piglowd.conf
	@sudo cp piglowd.init /etc/init.d/piglowd
	@sudo chmod 755 /etc/init.d/piglowd

