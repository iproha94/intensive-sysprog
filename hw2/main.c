#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/sem.h>
#include <stdio.h>

#define COUNT_CHILD 10

int main() {
	key_t key;
	int semid;

	key = ftok("/etc/fstab", getpid());
	semid = semget(key, COUNT_CHILD + 1, 0666 | IPC_CREAT);

	if (semid == -1) {
		printf("ERROR: create sem");
		exit(-1);
	}
	
	union semun {
		int val;                  /* значение для SETVAL */
		struct semid_ds *buf;     /* буферы для  IPC_STAT, IPC_SET */
		unsigned short *array;    /* массивы для GETALL, SETALL */
		                          /* часть, особенная для Linux: */
		struct seminfo *__buf;    /* буфер для IPC_INFO */
	} arg;

	/* в семафоре 0 установить значение 1 */
	arg.val = 1;
	if (semctl(semid, 0, SETVAL, arg) == -1) {
		printf("ERROR: set init sem");
		exit(-1);
	}

	int parent = 1;
	int my_id;
	for (int i = 0; parent && i < COUNT_CHILD; ++i) {
		parent = fork();
		my_id = i;

		if (parent == -1) {
			printf("ERROR: run fork\n");
			exit(-1);
		}
	}

	if (parent) {
		sleep(5);

		/* удалить семафор */
		if (semctl(semid, 0, IPC_RMID) == -1) {
			printf("ERROR: close sem");
			exit(-1);
		}

		printf("PARENT: I finished\n");
		exit(0);
	}

	printf("CHILD: My id = %d\n", my_id);

	struct sembuf lock_res = {my_id, -1, 0};
	struct sembuf rel_res = {my_id + 1, 1, 0};
	
	if (semop(semid, &lock_res, 1) == -1){
		printf("ERROR: semop lock_res");
	}

	printf("\t\t child say: %d\n", my_id);

	if (semop(semid, &rel_res, 1) == -1){
		printf("ERROR: semop rel_res");
	}

	exit(0);
}
