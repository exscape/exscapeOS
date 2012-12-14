#ifndef _ORDERED_ARRAY_H
#define _ORDERED_ARRAY_H

#include <sys/types.h>

/* This array is sorted on insertion and thus is always sorted. It stores anything that can be cast to a void*. */
typedef void* type_t;

/* A predicate that should return nonzero if the first argument is less than the second. */
typedef sint8 (*lessthan_predicate_t)(type_t, type_t);

typedef struct {
	type_t *array;
	uint32 size;
	uint32 max_size;
	lessthan_predicate_t less_than;
} ordered_array_t;

sint8 standard_lessthan_predicate(type_t a, type_t b);

/* Functions to create an ordered array */
ordered_array_t create_ordered_array(uint32 max_size, lessthan_predicate_t less_than);
ordered_array_t place_ordered_array(void *addr, uint32 max_size, lessthan_predicate_t less_than);

void destroy_ordered_array(ordered_array_t *array);
void insert_ordered_array(type_t item, ordered_array_t *array);

/* Replace an element */
void update_ordered_array(uint32 i, type_t item, ordered_array_t *array);

/* Returns the index of the element specified, or -1 if not found. If multiple exists, returns the first item. */
sint32 indexof_ordered_array(type_t item, ordered_array_t *array);

/* Equivalent to array[i] */
type_t lookup_ordered_array(uint32 i, ordered_array_t *array);

/* Remove an item by index */
void remove_ordered_array(uint32 i, ordered_array_t *array);

/* Remove an item by the item itself */
void remove_ordered_array_item(type_t item, ordered_array_t *array);

#endif
