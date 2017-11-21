#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

#define FNAME "file.txt"
#define MAX_SIZE_STR 10

int main() {
	sleep(1);
	FILE *f = fopen(FNAME, "a+");

	struct flock lock;
	memset(&lock, 0, sizeof(struct flock));
	
	lock.l_type = F_WRLCK;
	fcntl(fileno(f), F_SETLKW, &lock);
	
	char str[MAX_SIZE_STR];
	while (fgets(str, MAX_SIZE_STR, f) != NULL);

	int i = atoi(str);
	++i;
	fprintf(f, "%d\n", i);
	fflush(f);

	lock.l_type = F_UNLCK;
	fcntl(fileno(f), F_SETLKW, &lock);
	
	fclose(f);

	exit(0);	
}
