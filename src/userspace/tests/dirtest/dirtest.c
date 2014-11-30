#include <dirent.h>     /* Defines DT_* constants */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

extern int getdents(int, void *, int);

#define handle_error(msg) \
	   do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define BUF_SIZE 1024

int
main(int argc, char *argv[])
{
   int fd, nread;
   char buf[BUF_SIZE];
   struct dirent *d;
   int bpos;
   char d_type;

   fd = open(argc > 1 ? argv[1] : ".", O_RDONLY);
   if (fd == -1)
	   handle_error("open");

   for ( ; ; ) {
		nread = getdents(fd, buf, BUF_SIZE);
	   if (nread == -1)
		   handle_error("getdents");

	   if (nread == 0)
		   break;
	  printf("--------------- nread=%d ---------------\n", nread);
	   printf("i-node#  file type  d_reclen  d_namlen   d_name\n");
	   for (bpos = 0; bpos < nread;) {
		   d = (struct dirent *) (buf + bpos);
		   printf("%8d  ", (int)d->d_ino);
		   d_type = d->d_type;
		   printf("%-10s ", (d_type == DT_REG) ?  "regular" :
							(d_type == DT_DIR) ?  "directory" :
							(d_type == DT_FIFO) ? "FIFO" :
							(d_type == DT_SOCK) ? "socket" :
							(d_type == DT_LNK) ?  "symlink" :
							(d_type == DT_BLK) ?  "block dev" :
							(d_type == DT_CHR) ?  "char dev" : "???");
		   printf("%4d %10d  %s\n", d->d_reclen,
				   (int) d->d_namlen, d->d_name);
		   bpos += d->d_reclen;
	   }
   }

   exit(EXIT_SUCCESS);
}
