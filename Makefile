all: release

flags = -Wall -Wextra
libs = -lraylib -lm -lGL

release: main.c
	gcc $(libs) $(flags) -O3 -o main main.c

debug: main.c
	gcc $(libs) $(flags) -O0 -g -o main-debug main.c

gpu-test: gpu_cube.c
	gcc $(libs) -lGL $(flags) -o gpu_cube gpu_cube.c