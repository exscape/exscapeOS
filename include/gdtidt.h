#ifndef _GDTIDT_H
#define _GDTIDT_H

void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran);
void gdt_install(void);
void gdt_flush(void);

void idt_install(void);

#endif
