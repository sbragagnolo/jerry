#include<stdio.h>
#include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>
#include <unistd.h>


int main (void) {
	
	char buffer [3];
	int mouse = open ("/dev/input/mouse0", 0, O_RDONLY) ;
	
	printf ("%d mouse \n", mouse);
	

	while ( read(mouse, buffer, 3) >0) {
		printf("%d %d %d \n", buffer[0], buffer[1], buffer[2]);
	} 
	

	return 0;
}
