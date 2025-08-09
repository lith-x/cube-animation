all: release

flags = -Wall -Wextra
libs = -lraylib -lm

release: main.c
	gcc $(libs) $(flags) -O3 -o main main.c

debug: main.c
	gcc $(libs) $(flags) -O0 -g -o main-debug main.c