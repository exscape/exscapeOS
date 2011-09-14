#include <types.h>
#include <kernel/ordered_array.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <string.h>
#include <stdio.h> /* sprintf, for a debugging change only! */
#include <kernel/monitor.h> /* printk, same as above */

sint8 standard_lessthan_predicate(type_t a, type_t b) {
	return (a < b) ? 1 : 0;
}

/* Create an ordered array */
ordered_array_t create_ordered_array(uint32 max_size, lessthan_predicate_t less_than) {
	ordered_array_t arr;

	/* allocate one element extra; remove_ordered_array accesses one element past the end.
	 * ugly, but easier (and prettier code, even if the idea is dirty) than changing the algorithm. */
	arr.array = (void *)kmalloc( (max_size + 1) * sizeof(type_t));
	memset(arr.array, 0, max_size * sizeof(type_t));
	arr.size = 0;
	arr.max_size = max_size;
	arr.less_than = less_than;

	return arr;
}

/* Create an ordered array at /addr/ (doesn't allocate space!) */
ordered_array_t place_ordered_array(void *addr, uint32 max_size, lessthan_predicate_t less_than) {
	ordered_array_t arr;
	arr.array = (type_t *)addr;
	memset(arr.array, 0, max_size * sizeof(type_t));
	arr.size = 0;
	arr.max_size = max_size;
	arr.less_than = less_than;

	return arr;
}

/* Destroy an ordered array */
void destroy_ordered_array(ordered_array_t *array) {
	/* kfree(array->array); */
}

/* Insert an item into an ordered array */
void insert_ordered_array(type_t item, ordered_array_t *array) {
	assert(array->less_than != NULL);
	assert(array->size + 1 <= array->max_size);

	uint32 i = 0;


	/* TODO: remove this when the heap works 100% and has done so for a while */
	/* This makes sure NO duplicate addresses are allowed. */
	uint32 tmp = 0;
	for (tmp = 0; tmp < array->size; tmp++) {
		if ((type_t)lookup_ordered_array(tmp, array) == item) {
			header_t *header = (header_t *)item;
			footer_t *footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t) );
			printk("Tried to insert duplicate address %p into the heap map!\n",item);

			printk("Entry exists as a %s of %d bytes (%s header magic, %s footer magic, %s header pointer\n",
					(header->is_hole ? "hole" : "block"),
					header->size,
					(header->magic == HEAP_MAGIC ? "valid" : "INVALID"),
					(footer->magic == HEAP_MAGIC ? "valid" : "INVALID"),
					(footer->header == header ? "valid" : "INVALID")
				  );

			printk("Running validate_heap_index ...\n\n");
			validate_heap_index();
			printk("Heap validation passed.\n");
			panic("See above");
		}
	}

	/* Figure out where the item should be placed */
	while (i < array->size && array->less_than(array->array[i], item))
		i++;

	if (i == array->size) {
		/* Just add this item at the end of the array */
		array->array[array->size] = item;
	}
	else {
		/* Save the current item */
		type_t tmp = array->array[i];

		array->array[i] = item;

		/* Move the rest of the array one step forwards */
		while (i < array->size) {
			i++;
			type_t tmp2 = array->array[i];
			array->array[i] = tmp;
			tmp = tmp2;
		}
	}

	array->size++;
}

sint32 indexof_ordered_array(type_t item, ordered_array_t *array) {
	/* Returns the index where this item is stored (or the first, if it exists multiple times), or -1 if nothing is found. */
	sint32 i = 0;
	for (i = 0; i < array->size; i++) {
		if (lookup_ordered_array(i, array) == item)
			return i;
	}

	return -1;
}


void update_ordered_array(uint32 i, type_t item, ordered_array_t *array) {
	assert(i < array->size);

	/* TODO: remove this when the heap works 100% and has done so for a while */
	/* This makes sure NO duplicate addresses are allowed. */
	uint32 tmp = 0;
	for (tmp = 0; tmp < array->size; tmp++) {
		if ((type_t)lookup_ordered_array(tmp, array) == item) {
			if (tmp == i) {
				/* Attemped to overwrite the same index with the same value, i.e. no change, and nothing bad */
				return;
			}

			header_t *header = (header_t *)item;
			footer_t *footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t) );
			printk("Tried to insert duplicate address %p into the heap map!\n",item);

			printk("Entry exists as a %s of %d bytes (%s header magic, %s footer magic, %s header pointer\n",
					(header->is_hole ? "hole" : "block"),
					header->size,
					(header->magic == HEAP_MAGIC ? "valid" : "INVALID"),
					(footer->magic == HEAP_MAGIC ? "valid" : "INVALID"),
					(footer->header == header ? "valid" : "INVALID")
				  );
			printk("Current value at index %d (which would have been overwritten) is %p\n", i, (void *)lookup_ordered_array(i, array));

			printk("Running validate_heap_index ...\n\n");
			validate_heap_index();
			printk("Heap validation passed.\n");
			panic("See above");
		}
	}


	array->array[i] = item;
}

type_t lookup_ordered_array(uint32 i, ordered_array_t *array) {
	assert(i < array->size);
	return array->array[i];
}

/* Remove the object at i from the array */
void remove_ordered_array(uint32 i, ordered_array_t *array) {
	/* Shrink the array, overrwriting the element to remove */
	while (i < array->size) {
		array->array[i] = array->array[i+1];
		i++;
	}

	array->size--;
}
