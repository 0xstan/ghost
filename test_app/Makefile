CC=gcc
CFLAGS=
LFLAGS=

all: injector target

injector: injector.c
	$(CC) -no-pie -fno-pie $(CFLAGS) $^ -o $@ $(LFLAGS)

target: target.c
	$(CC) -no-pie -fno-pie $(CFLAGS) $^ -o $@ $(LFLAGS)

clean:
	rm injector target
