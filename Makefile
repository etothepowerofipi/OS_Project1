all:
	make clean
	clear
	make run

main: main.o
	gcc -o main main.o -lpthread -lrt

main.o: main.c
	gcc -c main.c

clean:
	rm *.o main output/*

run: main
	./main a.txt 100

valgrind: main
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./main a.txt 100 
