#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	   for (int i=0; i < argc; i++) {
		          printf("argv[%d] = %s\n", i, argv[i]);
				     }
	  int c;
	 while ((c = getopt(argc, argv, "abc")) != -1) {
		 switch (c) {
			 case 'a':
				  printf("option a\n");
					break;
			 case 'b':
					 printf("option b\n");
					   break;
			 case 'c':
						printf("option c\n");
						  break;
			 default:
				printf("valid options are: -a, -b and -c\n");
						  exit(1);
						   break;
		 }
	 }

	 return 0;
}
