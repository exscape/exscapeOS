#ifndef _TYPES_H
#define _TYPES_H

typedef unsigned long size_t;
typedef signed long ssize_t;

typedef struct Point {
	unsigned int x;
	unsigned int y;
} Point;

typedef struct Time {
	unsigned short year;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
} Time;

#endif
