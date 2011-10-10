#ifndef _LIST_H
#define _LIST_H

struct list; /* forward declaration due to its use in the node struct */

/* A single node in the list */
typedef struct node {
	void *data;
	struct node *prev;
	struct node *next;
	struct list *list; /* the list that this node belongs to */
} node_t;

typedef struct list {
	node_t *head;
	node_t *tail;
	uint32 count; /* number of items in the list */
} list_t;

list_t *list_create(void);
node_t *list_prepend(list_t *list, void *data);
node_t *list_append(list_t *list, void *data);
node_t *list_node_insert_before(node_t *node, void *data);
node_t *list_node_insert_after(node_t *node, void *data);
void list_remove(list_t *list, node_t *elem);
void list_destroy(list_t *list);
node_t *list_find_first(list_t *list, void *data);
node_t *list_find_last(list_t *list, void *data);

#endif
