all:
	gcc new_alarm_mutex.c -D_POSIX_PTHREAD_SEMANTICS -lpthread -lm
	./a.out