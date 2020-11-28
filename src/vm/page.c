#include "page.h"
#include "../userprog/syscall.h"

static unsigned supp_hash_func (const struct hash_elem *e, void *aux);
static bool supp_less_func (const struct hash_elem *a,
							const struct hash_elem *b,
							void *aux);
static void supp_destroy_func (struct hash_elem *e, void *aux);

/* Create a new Supplemental Page Table */
struct supp_pt* create_supp_pt(void) {
  struct supp_pt* spt = calloc (1, sizeof(struct supp_pt));

  if (!spt)
    exti_fail();

  hash_init(&spt->hash_table, supp_hash_func, supp_less_func, NULL);
  return spt;
}

/* Destroy Supplemental Page Table */
void destroy_supp_pt(struct supp_pt *spt) {
  hash_destroy(spt, supp_destroy_func);
  free(spt);
}

/* Hashing function for Supplemental Page Table */
static unsigned supp_hash_func (const struct hash_elem *e, void *aux)
{
  return hash_int( hash_entry(e, struct supp_pt_entry, elem)->upage );
}

/* Supplemental Page Table comparator */
static bool supp_less_func (const struct hash_elem *a,
							const struct hash_elem *b,
							void *aux)
{
  struct supp_pt_entry *a = hash_entry(a, struct supp_pt_entry, elem);
  struct supp_pt_entry *b = hash_entry(b, struct supp_pt_entry, elem);
  return a->upage < b->upage;
}

static void supp_destroy_func (struct hash_elem *e, void *aux)
{
  struct supp_pt_entry *entry = hash_entry(e, struct supp_pt_entry, elem);
  free (entry);
}

static bool install_frame(struct supp_pt *supp, void *upage, void *kpage)
{
  struct supp_pt_entry *entry;
  entry = calloc(1, sizeof(struct supp_pt_entry));

  entry->dirty_bit = false;
  entry->upage = upage;
  entry->kpage = kpage;

  struct hash_elem *prev_elem = hash_insert(&supp->hash_table, &entry->elem);
  if(prev_elem == NULL){      // check if the insert is successful
    return true;
  }
  else{
    free(entry);
    return false;
  }
}