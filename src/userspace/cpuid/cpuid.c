#include <stdio.h>

int main(int argc, char **argv) {
	int eax, ebx, ecx, edx;
	asm volatile("movl $1, %%eax;"
			     "cpuid;"
				 " mov %%eax, %[eaxout];"
				 " mov %%ebx, %[ebxout];"
				 " mov %%ecx, %[ecxout];"
				 " mov %%edx, %[edxout];"
				 : [eaxout] "=m"(eax),
				  [ebxout] "=m"(ebx),
				  [ecxout] "=m"(ecx),
				  [edxout] "=m"(edx)
				 :
				 : "eax", "ebx", "ecx", "edx");

#define EAX(n) ((eax & (1 << n)) ? "yes" : "no")
#define EBX(n) ((ebx & (1 << n)) ? "yes" : "no")
#define ECX(n) ((ecx & (1 << n)) ? "yes" : "no")
#define EDX(n) ((edx & (1 << n)) ? "yes" : "no")
	printf("FPU: %s\n", EDX(0));
	printf("MMX: %s\n", EDX(23));
	printf("FXSR: %s\n", EDX(24));
	printf("SSE: %s\n", EDX(25));
	printf("SSE2: %s\n", EDX(26));
	printf("SSE3: %s\n", ECX(0));
	printf("SSSE3: %s\n", ECX(9));
	printf("SSSE4.1: %s\n", ECX(19));
	printf("SSSE4.2: %s\n", ECX(20));
	printf("AVX: %s\n", ECX(28));
	printf("POPCNT: %s\n", ECX(23));
	printf("AES-NI: %s\n", ECX(25));

	return 0;
}
