#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <sys/wait.h>

struct pipe {
	int read;
	int write;
};

struct command {
	const char **cmd;
	struct pipe *in;
	struct pipe *out;
};

int main(int argc, char **argv) {

#define MAX_PIPES 64
	struct pipe pipes[MAX_PIPES];
	memset(&pipes, 0, MAX_PIPES * sizeof(struct pipe));

	/*
	const char *arg1[] = { "/bin/cat", "/fat/numbers", NULL };
	const char *arg2[] = { "/bin/grep", "5", NULL };
	const char *arg3[] = { "/bin/grep", "0", NULL };
	struct command cmd1 = { arg1, NULL, NULL };
	struct command cmd2 = { arg2, NULL, NULL };
	struct command cmd3 = { arg3, NULL, NULL };

	struct command *cmds[] = { &cmd1, &cmd2, &cmd3, NULL };

	int num_cmds = 3;
	*/

	const char *arg1[] = { "/bin/cat", "/fat/mm.mp3", NULL };
	const char *arg2[] = { "/bin/md5", NULL };
	struct command cmd1 = { arg1, NULL, NULL };
	struct command cmd2 = { arg2, NULL, NULL };

	struct command *cmds[] = { &cmd1, &cmd2, NULL };

	int num_cmds = 2;

	for (int i = 0; i < num_cmds - 1; i++) {
		pipe((int *)&pipes[i]); // TODO: error handling
		printf("pipe() created FDs %d (read) and %d (write)\n", pipes[i].read, pipes[i].write);
		assert(cmds[i]->cmd != NULL);
		assert(cmds[i+1]->cmd != NULL);

		printf("assigning pipe %d (r)/%d (w) as output for %s\n", pipes[i].read, pipes[i].write, cmds[i]->cmd[0]);
		cmds[i  ]->out = &pipes[i];
		printf("assigning pipe %d (r)/%d (w) as input for %s\n", pipes[i].read, pipes[i].write, cmds[i+1]->cmd[0]);
		cmds[i+1]->in  = &pipes[i];
	}
	fflush(stdout);

	for (int i = 0; i < num_cmds; i++) {
		fprintf(stderr, "forking, i = %d\n", i);
		int pid = fork();
		if (pid > 0) {
			// Parent

			if (num_cmds > 1) {
				// Close pipe FDs in the parent

				if (i == 0) {
					assert(!close(pipes[i].write));
				}
				else if (i > 0 && i < num_cmds - 1) {
					assert(!close(pipes[i-1].read));
					assert(!close(pipes[i].write));
				}
				else if (i == num_cmds - 1) {
					assert(!close(pipes[i].read));
				}
			}
		}
		else {
			// Child
			fprintf(stderr, "child cmds[%d], pid %d (%s) starting up...\n", i, getpid(), cmds[i]->cmd[0]);
			if (cmds[i]->in != NULL) {
				// Change stdin
				fprintf(stderr, "child cmds[%d], pid %d: redirecting %d to stdin and closing both pipe fds\n", i, getpid(), cmds[i]->in->read);
				int in_fd = cmds[i]->in->read;
				close(cmds[i]->in->write);
				int r;
				if ((r = dup2(in_fd, fileno(stdin))) != fileno(stdin)) {
					fprintf(stderr, "cmds[%d], pid %d: dup2(%d, stdin) failed! errno = %d (%s)\n", i, getpid(), in_fd, errno, strerror(errno));
				}
				close(in_fd);
			}
			if (cmds[i]->out != NULL) {
				// Change stdout
				fprintf(stderr, "child cmds[%d], pid %d: redirecting %d to stdout and closing both pipe fds\n", i, getpid(), cmds[i]->out->write);
				int out_fd = cmds[i]->out->write;
				close(cmds[i]->out->read);
				int r;
				if ((r = dup2(out_fd, fileno(stdout))) != fileno(stdout)) {
					fprintf(stderr, "cmds[%d], pid %d: dup2 to stdout failed! errno = %d (%s)\n", i, getpid(), errno, strerror(errno));
				}
				close(out_fd);
			}

			fprintf(stderr, "child cmds[%d]: executing %s\n", i, cmds[i]->cmd[0]);

			execve(cmds[i]->cmd[0], (char **)cmds[i]->cmd, NULL);
			fprintf(stderr, "EXECVE FAILED for cmd %d; errno = %d (%s)\n", i, errno, strerror(errno));
			abort();
		}
	}

	int status;
	int r;
	printf("parent: waiting for next child to exit...\n");
	while ((r = waitpid(-1, &status, 0)) != -1) { 
		printf("waitpid returned %d; exit status = 0x%04x\n", r, status);
	}

	return 0;
}
