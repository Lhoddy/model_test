SRC = server client
CFLAGS  := -Wall -Werror -g

vpath %.h .

all : $(SRC)

server: ip_server.o
	cc -o $@ $^ ${LDFLAGS}

client: ip_client.o
	cc -o $@ $^ ${LDFLAGS}

.PHONY : clean cleani cleano

clean : cleani cleano
	ls
cleani :
	-rm -f $(SRC)
cleano :
	-rm -f *.o