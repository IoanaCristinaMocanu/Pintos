#include "page.h"
#include "../userprog/syscall.h"
#include "../threads/vaddr.h"
#include "../threads/malloc.h"

static unsigned supp_hash_func (const struct hash_elem *e, void *aux UNUSED);
static bool supp_less_func (const struct hash_elem *a,
                            const struct hash_elem *b,
                            void *aux UNUSED);
static void supp_destroy_func (struct hash_elem *e, void *aux UNUSED);

/* Create a new Supplemental Page Table */
struct supp_pt *create_supp_pt (void)
{
  struct supp_pt *spt = calloc (1, sizeof (struct supp_pt));

  if (!spt)
    exit_fail ();

  hash_init (&spt->hash_table, supp_hash_func, supp_less_func, NULL);
  return spt;
}

/* Destroy Supplemental Page Table */
void destroy_supp_pt (struct supp_pt *spt)
{
  hash_destroy (&spt->hash_table, supp_destroy_func);
  free (spt);
}

/* Install the frame corresponding */
bool install_frame (struct supp_pt *supp, void *upage, void *kpage)
{
  struct supp_pt_entry *entry = calloc (1, sizeof (struct supp_pt_entry));

  if(!entry)
    exit_fail();

  entry->dirty_bit = false;
  entry->upage = upage;
  entry->kpage = kpage;

  struct hash_elem *prev_elem = hash_insert (&supp->hash_table, &entry->list_elem);
  if (prev_elem == NULL)
  {      // check if the insert is successful
    return true;
  } else
  {
    free (entry);
    return false;
  }
}

/* Install a page of type ZERO */
bool install_page_zero (struct supp_pt *supp, void *upage)
{
  struct supp_pt_entry *entry = calloc (1, sizeof (struct supp_pt_entry));

  if(!entry)
    exit_fail();

  entry->dirty_bit = false;
  entry->upage = upage;
  entry->kpage - NULL;
  entry->page_status = ZERO;

  struct hash_elem *prev_elem = hash_insert (&supp->hash_table, &entry->list_elem);
  if (prev_elem == NULL)    // check if the insert is successful
    return true;
  else
    return false;
}

/* Get the requested user page from the hash table */
struct supp_pt_entry *find_page (struct supp_pt *supp, void *upage)
{
  struct supp_pt_entry *entry = calloc (1, sizeof(struct supp_pt_entry));
  entry->upage = pg_round_down(upage);
  struct hash_elem *elem = hash_find(&supp->hash_table, &entry->list_elem);
  if(!elem)
    return NULL;
  return hash_entry(elem, struct supp_pt_entry, list_elem);
}

/* Check if the requested user page is in the hash table */
bool has_page (struct supp_pt *supp, void *upage)
{
  return find_page(supp, upage) != NULL;
}

/* Hashing function for Supplemental Page Table */
static unsigned supp_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  return hash_int (hash_entry(e, struct supp_pt_entry, list_elem)->upage);
}

/* Supplemental Page Table comparator */
static bool supp_less_func (const struct hash_elem *a,
                            const struct hash_elem *b,
                            void *aux UNUSED)
{
  struct supp_pt_entry *entry1 = hash_entry(a, struct supp_pt_entry, list_elem);
  struct supp_pt_entry *entry2 = hash_entry(b, struct supp_pt_entry, list_elem);
  return entry1->upage < entry2->upage;
}

static void supp_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
  struct supp_pt_entry *entry = hash_entry(e, struct supp_pt_entry, list_elem);
  free (entry);
}