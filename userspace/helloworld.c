int puts(const char *str) {
	// TODO: proper syscalls, without this mess!
	asm volatile("mov $1, %%eax; mov %[str], %%ebx; int $0x80" : : [str]"r"(str) : "cc", "memory", "%ebx", "%eax");
	return 0;
}

char some_data[] = "Global char array";
int global_int;
int main(int argc, char **argv) {
	int var = 2;
	int test = 0;
	char *str = "Hello, ELF world!\n";
	char *str2 = "String #2";
	char *world = "world!";
	char test_arr[8] = {0};
	puts(str);

	return 0;
}
