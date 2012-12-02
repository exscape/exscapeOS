#include <exscapeos.h>

int main(int argc, char **argv) {
	/* A simple "guess the number" game. 1-digit number due to the lack of simple library functions
	 * for keyboard input. */

	int num = 3; // TODO: rand() in userspace
	int guess = -1;
	int num_guesses = 0;

	while (guess != num) {
		num_guesses++;
		puts("Guess the number, from user mode (0-9): ");
		guess = getchar();
		putchar(guess);
		puts("  ");
		guess -= 0x30; /* ASCII to num */
		if (guess == num) {
			puts("You got it!\n");
			break;
		}
		else if (guess > num) {
			puts("Nope. Try lower.\n");
		}
		else if (guess < num) {
			puts("Nope. Try higher.\n");
		}
	}

	return 0;
}
