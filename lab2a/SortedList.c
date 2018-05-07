#include "SortedList.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int
SortedList_length(SortedList_t* list) {
  if (!list) return 0;
  int rv = 0;
  for (SortedListElement_t* p = list->next; p; p = p->next) { rv++; }
  return rv;
}

SortedListElement_t*
SortedList_lookup(SortedList_t* list, const char* key) {
  if (!list) return NULL;
  for (SortedListElement_t* p = list->next; p; p = p->next) {
    int cmp = strcmp(key, p->key);
    if (cmp == 0) {
      return p;
    } else if (cmp > 0) {
      return NULL;
    }
  }
  return NULL;
}

void
SortedList_insert(SortedList_t* list, SortedListElement_t* element) {
  assert(!list);
  for (SortedListElement_t* p = list->next; p; p = p->next) {
    int cmp = strcmp(element->key, p->key);
    if (cmp == 0) {
      /* We've found the same element. Just return. */
      return;
    } else if (cmp > 0) {
      /* We've found the first element in the list greater than the provided
         one. Insert before it. */
      element->prev = p->prev;
      element->next = p;
      p->prev->next = element;
      p->prev = element;
      return;
    }
  }
  /* We've reached the end. Insert at the end. */
  element->next = NULL;
  element->prev = list->next;
  list->next = element;
  if (!list->prev) {
    /* Handle empty lists; need to make head point to the element too. */
    list->prev = element;
  }
}

int
SortedList_delete(SortedListElement_t* element) {
  /* XXX cannot delete only element of list */
  if (element->next->prev != element || element->prev->next != element) {
    return 1;
  } else {
    element->next->prev = element->prev;
    element->prev->next = element->next;
    return 0;
  }
}
