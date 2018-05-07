#include "SortedList.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/** NOTE: The deletion operation requires the ability to delete any node given
 * just the node itself. This means that a typical non-circular doubly linked
 * list won't work, because we can't update the list's head/tail pointers if we
 * delete the head or tail (or the only element). Furthermore, it is required
 * that for all elements we have to check el->next->prev and el->prev->next
 * point to this node.
 *
 * So this is a modified design where the prev pointer of the head points to the
 * list itself, and similarly for the next pointer of the tail. This ensures
 * that head->prev->next == head and tail->next->prev == tail. If there is only
 * one element in the list, then that element's next and prev both point to the
 * list. The empty list has both head and tail point as the list itself. This
 * means that the following loop in this pattern works for lists in all cases:
 * for (auto p = list->head; p != list; p = p->next) {...}
 */

#define head next
#define tail prev

int
SortedList_length(SortedList_t* list) {
  if (!list) return 0;
  int rv = 0;
  for (SortedListElement_t* p = list->head; p != list; p = p->next) { rv++; }
  return rv;
}

SortedListElement_t*
SortedList_lookup(SortedList_t* list, const char* key) {
  if (!list) return NULL;
  for (SortedListElement_t* p = list->head; p != list; p = p->next) {
    int cmp = strcmp(key, p->key);
    if (cmp == 0) {
      return p;
    } else if (cmp < 0) {
      return NULL;
    }
  }
  return NULL;
}

void
SortedList_insert(SortedList_t* list, SortedListElement_t* element) {
  assert(list);
  assert(element->key);
  for (SortedListElement_t* p = list->head; p != list; p = p->next) {
    assert(p->key);
    int cmp = strcmp(element->key, p->key);
    if (cmp == 0) {
      /* We've found the same element. Just return. */
      return;
    } else if (cmp < 0) {
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
  element->next = list;
  element->prev = list->tail;
  list->tail = element;
  if (list->head == list->tail) {
    /* Empty or singleton list */
    list->head->next = element;
  } else {
    /* Multi-element list */
    list->tail->prev->next = element;
  }
}

int
SortedList_delete(SortedListElement_t* element) {
  if (!element->next || !element->prev) {
    /* Double delete. */
    return 1;
  }
  if (element->next->prev != element || element->prev->next != element) {
    return 1;
  } else {
    element->next->prev = element->prev;
    element->prev->next = element->next;
    element->next = NULL;
    element->prev = NULL;
    return 0;
  }
}

#undef head
#undef tail
