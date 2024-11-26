#ifndef _DA_H
#define _DA_H

#define da_grow_cap(da)                                                        \
  (da)->cap == 0 ? sizeof(*(da)->items) > 1024 ? 1 : 256 : (da)->cap * 2

#define da_append(da, item)                                                    \
  do {                                                                         \
    if ((da)->len >= (da)->cap) {                                              \
      (da)->cap = da_grow_cap(da);                                             \
      (da)->items = realloc((da)->items, (da)->cap * sizeof(*(da)->items));    \
      assert((da)->items != NULL);                                             \
    }                                                                          \
    (da)->items[(da)->len++] = (item);                                         \
  } while (0)

#define da_reserve(da, add)                                                    \
  do {                                                                         \
    if ((da)->cap >= (da)->len + (add))                                        \
      break;                                                                   \
    while ((da)->len + (add) > (da)->cap) {                                    \
      (da)->cap = da_grow_cap(da);                                             \
    }                                                                          \
    (da)->items = realloc((da)->items, (da)->cap * sizeof(*(da)->items));      \
    assert((da)->items != NULL);                                               \
  } while (0)

#define da_append_many(da, new_items, items_count)                             \
  do {                                                                         \
    if ((da)->len + items_count > (da)->cap) {                                 \
      while ((da)->len + items_count > (da)->cap) {                            \
        (da)->cap = da_grow_cap(da);                                           \
      }                                                                        \
      (da)->items = realloc((da)->items, (da)->cap * sizeof(*(da)->items));    \
      assert((da)->items != NULL);                                             \
    }                                                                          \
    memcpy((da)->items + (da)->len, new_items,                                 \
           items_count * sizeof(*(da)->items));                                \
    (da)->len += items_count;                                                  \
  } while (0)

#endif
