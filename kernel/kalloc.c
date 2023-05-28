// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void check(void);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
int ref_count[((PHYSTOP - KERNBASE) / PGSIZE) + 1];
struct spinlock ref_lock;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&ref_lock, "refcount");
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    acquire(&ref_lock);
    ref_count[MEMINDEX(p)] = 1;
    release(&ref_lock);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  // check();
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  acquire(&ref_lock);
  if (ref_count[MEMINDEX(pa)] <= 0) {
    printf("%p %d %d", pa, ref_count[MEMINDEX(pa)], MEMINDEX(pa));
    panic("kfree: negtive refcount");
  }
  ref_count[MEMINDEX(pa)]--;
  if (ref_count[MEMINDEX(pa)] != 0) {
    release(&ref_lock);
    return;
  }
  release(&ref_lock);
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  // check();
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  
  if(r) {
    acquire(&ref_lock);
    if (ref_count[MEMINDEX(r)] != 0) {
      printf("%p %d %d", r, ref_count[MEMINDEX(r)], MEMINDEX(r));
      panic("kalloc: refcount");
    }
    ref_count[MEMINDEX(r)]++;
    release(&ref_lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  
  return (void*)r;
}


void addref(uint64 pa)
{
  acquire(&ref_lock);
  ref_count[MEMINDEX(pa)]++;
  release(&ref_lock);
}

void subref(uint64 pa)
{
  acquire(&ref_lock);
  ref_count[MEMINDEX(pa)]--;
  release(&ref_lock);
}

int accref(uint64 pa)
{
  int ret = 0;
  acquire(&ref_lock);
  ret = ref_count[MEMINDEX(pa)];
  release(&ref_lock);
  return ret;
}

void check(void)
{
  struct run *r;

  acquire(&kmem.lock);
  acquire(&ref_lock);
  r = kmem.freelist;
  if(r) {
    if (ref_count[MEMINDEX(r)] != 0)
      panic("check refcount");
    r = r->next;
  }
  release(&kmem.lock);
  release(&ref_lock);
}