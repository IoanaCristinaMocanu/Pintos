#ifndef _PAGE_H_
#define _PAGE_H_

#include <hash.h>

struct supp_pt {
  struct hash hash_table;
};

struct supp_pt_entry {

  /* Virtual address of the user page */
  void *upage;

  /* Pointer to kernel page */
  void *kpage;

  /* Hash elem: */
  struct hash_elem elem;

  bool dirty_bit;

};

/* Create a new Supplemental Page Table */
struct supp_pt* create_supp_pt(void);
/* Destroy Supplemental Page Table */
void destroy_supp_pt(struct supp_pt *spt);

#endif //_PAGE_H_
