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
struct supp_pt_entry *
install_frame (struct supp_pt *supp, void *upage, void *kpage)
{
  struct supp_pt_entry *entry = calloc (1, sizeof (struct supp_pt_entry));

  if (!entry)
    exit_fail ();

  entry->dirty_bit = false;
  entry->upage = upage;
  entry->kpage = kpage;

  struct hash_elem *prev_elem = hash_insert (&supp->hash_table,
                                             &entry->list_elem);

  /* Check if insert was successful */
  if (prev_elem != NULL)
  {
    free (entry);
    return NULL;
  }
  return entry;
}

/* Install a page of type ZERO */
struct supp_pt_entry *install_page_zero (struct supp_pt *supp, void *upage)
{
  struct supp_pt_entry *entry = calloc (1, sizeof (struct supp_pt_entry));

  if (!entry)
    exit_fail ();

  entry->dirty_bit = false;
  entry->upage = upage;
  entry->kpage - NULL;
  entry->page_status = ZERO;

  struct hash_elem *prev_elem = hash_insert (&supp->hash_table,
                                             &entry->list_elem);

  /* Check if insert was successful */
  if (prev_elem != NULL)
  {
    free (entry);
    return NULL;
  }
  return entry;
}

/* Get the requested user page from the hash table */
struct supp_pt_entry *find_page (struct supp_pt *supp, void *upage)
{
  struct supp_pt_entry *entry = calloc (1, sizeof (struct supp_pt_entry));
  entry->upage = pg_round_down (upage);
  struct hash_elem *elem = hash_find (&supp->hash_table, &entry->list_elem);
  if (!elem)
    return NULL;
  return hash_entry(elem, struct supp_pt_entry, list_elem);
}

/* Check if the requested user page is in the hash table */
bool has_page (struct supp_pt *supp, void *upage)
{
  return find_page (supp, upage) != NULL;
}

struct supp_pt_entry *
install_page_file (struct supp_pt *supp, void *upage, void *kpage,
                   struct file *file, off_t
                   offset, uint32_t read_bytes, uint32_t zero_bytes,
                   bool writable)
{
  struct supp_pt_entry *entry = calloc (1, sizeof (struct supp_pt_entry));

  if (!entry)
    exit_fail ();

  entry->page_status = FILE_SYS;
  entry->upage = upage;
  entry->kpage = kpage;
  entry->dirty_bit = false;
  entry->file = file;
  entry->offset = offset;
  entry->zero_bytes = zero_bytes;
  entry->read_bytes = read_bytes;
  entry->writable = writable;

  struct hash_elem *prev_elem = hash_insert (&supp->hash_table,
                                             &entry->list_elem);

  /* check if insert was successful */
  if (prev_elem != NULL)
  {
    free (entry);
    return NULL;
  }
  return entry;
}

bool load_page (struct supp_pt *supp, void *upage)
{
  struct supp_pt_entry *entry = find_page(supp, upage);

  if(entry == NULL)
    return false;

  /* void *frame = allocate frame
  if(frame == NULL)
    return false;
  */

  /*
  bool writable;
  switch(entry->page_status)
  {
    case ZERO:
      break;
    case SWAPPED:
      break;
    case FILE_SYS:
      break;
    case IN_FRAME:
      return true;
    default:
  }
  */

  return true;
}

/* Hashing function for Supplemental Page Table */
static unsigned supp_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  return hash_int ((int) hash_entry(e, struct supp_pt_entry, list_elem)->upage);
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