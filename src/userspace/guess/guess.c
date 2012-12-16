#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	srand(848214); // TODO: time()
	int num = 1 + (rand() % 100);
	int guess = -1;
	int num_guesses = 0;

	while (guess != num) {
		num_guesses++;
		fputs("Guess the number, from user mode (1-100): ", stdout);
		fflush(stdout);

		scanf("%d", &guess);

		if (guess == num) {
			puts("You got it!");
			break;
		}
		else if (guess > num) {
			puts("Nope. Try lower.");
		}
		else if (guess < num) {
			puts("Nope. Try higher.");
		}
	}

	return 0;
}
