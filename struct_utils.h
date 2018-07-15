#ifndef STRUCT_UTILS_H
#define STRUCT_UTILS_H

#include <stdlib.h>
#include <stdio.h>

typedef struct List {
	struct List* next;
	long elem;
	int len;
} List;

//returns a new List head linked to the input List, new head contains elem

List* add_elem(long elem, List* list) {
	List* new_list = malloc(sizeof(List));
	new_list->elem = elem;
	if (list != NULL) {
		new_list->next = list;
		new_list->len = list->len + 1;
	} else {
		new_list->next = NULL;
		new_list->len = 1;
	}
	return new_list;
}

//returns a List consisting of the input Lists

List* merge_lists(List* list1, List* list2) {
	if (list1 == NULL) {
		return list2;
	}
	if (list2 == NULL) {
		return list1;
	}
	List* head;
	List* tail;
	if (list1->len <= list2->len) {
		head = list1;
		tail = list2;
	} else {
		head = list2;
		tail = list1;
	}
	List* node = head;
	int len = tail->len;
	while (1) {
		node->len += len;
		if (node->next == NULL) {
			break;
		}
		node = node->next;
	}
	node->next = tail;
	return head;
}

//removes the head of the input List, returns next node

List* rem_head(List* list) {
	List* new_head = list->next;
	free(list);
	return new_head;
}

//returns a new List containing the values of the input array

List* array_to_list(int len, long* array) {
	if (len <= 0 || array == NULL) {
		return NULL;
	}
	int i = len;
	List* node = malloc(sizeof(List));
	List* head = node;
	int a;
	for (a = 0; a < len; a++) {
		node->elem = array[a];
		node->len = i;
		i--;
		if (a != len - 1) {
			node->next = malloc(sizeof(List));
			node = node->next;
		} else {
			node->next = NULL;
		}
	}
	return head;
}

//returns a new array containing the elements of the input List

long* list_to_array(List* list) {
	long* array = malloc(list->len * sizeof(long));
	List* current = list;
	int a;
	for (a = 0; 1; a++) {
		array[a] = current->elem;
		if (current->next == NULL) {
			break;
		}
		current = current->next;
	}
	return array;
}

//deallocates the input List

void free_list(List* list) {
	if (list == NULL) {
		return;
	}
	List* current = list;
	List* next = current->next;
	while (1) {
		free(current);
		if (next == NULL) {
			break;
		}
		current = next;
		next = current->next;
	}
}

long list_size(List* list) {
	return list->len * sizeof(List);
}

//helper function to compare two integers

int comp_ints(const void* a, const void* b) {
	if (*(int*) a < *(int*) b) {
		return -1;
	} else if (*(int*) a > *(int*) b) {
		return 1;
	} else {
		return 0;
	}
}

//returns max input integer

int max_ints(int a, int b) {
	return a < b ? b : a;
}

#endif // STRUCT_UTILS_H
