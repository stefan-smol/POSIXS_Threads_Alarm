all:
	gcc -o a.out new_alarm_mutex.c -D_POSIX_PTHREAD_SEMANTICS -lpthread
	./a.out