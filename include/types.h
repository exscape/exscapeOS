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

// TODO: ensure that these types are accurate for more platforms than mine!
typedef signed   char  sint8;
typedef unsigned char  uint8;
typedef signed   short sint16;
typedef unsigned short uint16;
typedef signed   int   sint32;
typedef unsigned int   uint32;

#endif
