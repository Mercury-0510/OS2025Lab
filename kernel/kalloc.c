// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define CPUS 3  

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock[CPUS]; // 每个 CPU 的锁
  struct run *freelist[CPUS];   // 每个 CPU 的空闲链表
} kmem;

void
kinit()
{
  for (int i = 0; i < CPUS; i++) {
    initlock(&kmem.lock[i], "kmem_lock_cpu");
    kmem.freelist[i] = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{ 
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
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
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int cpu_id = ((uint64)pa / PGSIZE) % CPUS;

  acquire(&kmem.lock[cpu_id]);
  r->next = kmem.freelist[cpu_id];
  kmem.freelist[cpu_id] = r;
  release(&kmem.lock[cpu_id]);
}

// 分配空闲链表
struct run* 
search_free(int cpu_id) {
  struct run *r = 0;
  int cnt = 0;
  int limit = 100 * CPUS;
  for(int i = 0; cnt < limit; i = (i+1)%CPUS, cnt++) {
    if(kmem.freelist[i]) {
      acquire(&kmem.lock[i]);
      if(!kmem.freelist[i]) {
        release(&kmem.lock[i]);
        continue;
      }
      r = kmem.freelist[i];
      kmem.freelist[i] = r->next;
      release(&kmem.lock[i]);
      break;
    }
  }
  return r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu_id = cpuid();

  acquire(&kmem.lock[cpu_id]);
  r = kmem.freelist[cpu_id];

  if(r)
    kmem.freelist[cpu_id] = r->next;

  release(&kmem.lock[cpu_id]);

  if(!r) {
    r = search_free(cpu_id);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
