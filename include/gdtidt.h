#ifndef _GDTIDT_H
#define _GDTIDT_H

void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran);
void gdt_install(void);
void gdt_flush(void);

void idt_install(void);

typedef struct registers
{
   uint32 ds;
   uint32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
   uint32 int_no, err_code;
   uint32 eip, cs, eflags, useresp, ss; // Pushed by the processor automatically.
} registers_t; 

#endif
