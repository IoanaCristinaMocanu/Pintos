#ifndef _PAGE_H_
#define _PAGE_H_

#include <hash.h>

/* enum for the Page State */
enum page_status {
  ZERO,
  SWAPPED,
  FILE_SYS,
  IN_FRAME
};

struct supp_pt {
  struct hash hash_table;
};

struct supp_pt_entry {

  /* Virtual address of the user page */
  void *upage;

  /* Pointer to kernel page */
  void *kpage;

  /* Hash elem: */
  struct hash_elem list_elem;

  bool dirty_bit;

  enum page_status page_status;

};

/* Create a new Supplemental Page Table */
struct supp_pt* create_supp_pt(void);
/* Destroy Supplemental Page Table */
void destroy_supp_pt(struct supp_pt *spt);

/* Install the frame corresponding */
bool install_frame (struct supp_pt *supp, void *upage, void *kpage);
/* Install a page of type ZERO */
bool install_page_zero (struct supp_pt *supp, void *upage);

/* Get the requested user page from the hash table */
struct supp_pt_entry *find_page (struct supp_pt *supp, void *upage);

/* Check if the requested user page is in the hash table */
bool has_page (struct supp_pt *supp, void *upage);

/* lazy loading */
bool load_page (struct supp_pt *supp, void *upage);

#endif //_PAGE_H_
