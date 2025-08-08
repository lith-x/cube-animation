all: main

flags = -Wall -Wextra
libs = -lraylib

main: main.c
	gcc $(libs) $(flags) -o main main.c