#include "SortedList.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define head next
#define tail prev

void*
xmalloc(size_t s) {
  void* p = malloc(s);
  if (!p) abort();
  return p;
}

SortedList_t* SortedList_new(void) {
  SortedList_t*l = xmalloc(sizeof(SortedList_t));
  l->key = NULL;
  l->head = l;
  l->tail = l;
  return l;
}

SortedListElement_t* new_element(const char*key) {
  SortedListElement_t*el = xmalloc(sizeof(SortedListElement_t));
  el->key = key;
  el->next = NULL;
  el->prev = NULL;
  return el;
}

int
main(void) {
  SortedList_t* l = SortedList_new();
  assert(SortedList_length(l) == 0);
  assert(SortedList_lookup(l, "abcd") == NULL);

  /* Ordering 0. */
  assert(l->tail == l);
  assert(l->head == l);

  SortedListElement_t* fst = new_element("abcd");
  SortedList_insert(l, fst);
  assert(SortedList_length(l) == 1);
  assert(SortedList_lookup(l, "abcd") == fst);
  assert(SortedList_lookup(l, "abcde") == NULL);
  assert(SortedList_lookup(l, "abc") == NULL);

  /* Repeated insert. */
  SortedList_insert(l, fst);
  assert(SortedList_length(l) == 1);

  /* Ordering 1. */
  assert(l->tail == fst);
  assert(l->head == fst);
  assert(fst->prev == l);
  assert(fst->next == l);

  SortedListElement_t* snd = new_element("xyz");
  SortedList_insert(l, snd);
  assert(SortedList_length(l) == 2);
  assert(SortedList_lookup(l, "abcd") == fst);
  assert(SortedList_lookup(l, "abcde") == NULL);
  assert(SortedList_lookup(l, "abc") == NULL);
  assert(SortedList_lookup(l, "xyz") == snd);

  /* Ordering 2. */
  assert(l->head == fst);
  assert(l->tail == snd);
  assert(fst->prev == l);
  assert(fst->next == snd);
  assert(snd->prev == fst);
  assert(snd->next == l);

  SortedListElement_t* mid = new_element("mmm");
  SortedList_insert(l, mid);
  assert(SortedList_length(l) == 3);
  assert(SortedList_lookup(l, "abcd") == fst);
  assert(SortedList_lookup(l, "abcde") == NULL);
  assert(SortedList_lookup(l, "abc") == NULL);
  assert(SortedList_lookup(l, "xyz") == snd);
  assert(SortedList_lookup(l, "mmm") == mid);

  /* Ordering final. */
  assert(l->head == fst);
  assert(l->tail == snd);
  assert(fst->prev == l);
  assert(fst->next == mid);
  assert(mid->prev == fst);
  assert(mid->next == snd);
  assert(snd->prev == mid);
  assert(snd->next == l);

  /* Deletion. */
  assert(SortedList_delete(mid) == 0);
  assert(SortedList_length(l) == 2);
  assert(SortedList_lookup(l, "abcd") == fst);
  assert(SortedList_lookup(l, "mid") == NULL);
  assert(SortedList_lookup(l, "xyz") == snd);

  assert(SortedList_delete(fst) == 0);
  assert(SortedList_length(l) == 1);
  assert(SortedList_lookup(l, "abcd") == NULL);
  assert(SortedList_lookup(l, "mid") == NULL);
  assert(SortedList_lookup(l, "xyz") == snd);

  assert(SortedList_delete(snd) == 0);
  assert(SortedList_length(l) == 0);

  assert(SortedList_delete(snd) == 1);
}
