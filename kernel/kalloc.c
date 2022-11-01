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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int refNum[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    acquire(&kmem.lock);
    kmem.refNum[((uint64)p - KERNBASE) / PGSIZE] = 1;
    release(&kmem.lock);
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

  // 减少 refNum
  decreaseRefNum((uint64)pa);

  // 只有 refNum 等于 0 才回收
  if(getRefNum((uint64)pa) == 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    // 刚分配, refNum 设置为 1
    kmem.refNum[((uint64)r - KERNBASE) / PGSIZE] = 1;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

int getRefNum(uint64 pa) {
  acquire(&kmem.lock);
  int cnt = kmem.refNum[(pa - KERNBASE) / PGSIZE];
  release(&kmem.lock);
  return cnt;
}

int increaseRefNum(uint64 pa) {
  acquire(&kmem.lock);
  kmem.refNum[(pa - KERNBASE) / PGSIZE]++;
  release(&kmem.lock);
  return 0;
}

int decreaseRefNum(uint64 pa) {
  acquire(&kmem.lock);
  kmem.refNum[(pa - KERNBASE) / PGSIZE]--;
  release(&kmem.lock);
  return 0;
}

int isCOW(pte_t *pte) {
  if(*pte & PTE_COW) {
    return 1;
  }
  return 0;
}

uint64 cowKalloc(pagetable_t pgTable, uint64 va) {
  va = PGROUNDDOWN(va);
  pte_t* pte = walk(pgTable, va, 0);//获取该页对应的第三级页表项
  uint64 pa = PTE2PA(*pte);//获取该虚拟地址对应的物理地址的页起始地址

  *pte |= PTE_W;
  *pte &= ~PTE_COW;

  // 如果 refNum 为 1, 去掉 PTE_COW 后就可以直接返回了
  if(getRefNum(pa) == 1) {
    return pa;
  }

  // 下面是一般情况的处理
  decreaseRefNum(pa);

  char *mem;
  if((mem = kalloc()) == 0) {
    return 0;
  }

  uint flags = PTE_FLAGS(*pte);
  memmove(mem, (char*)pa, PGSIZE);
  *pte &= ~PTE_V; // 先将虚拟地址解绑, 否则下面的映射肯定不成功
  if(mappages(pgTable, va, PGSIZE, (uint64)mem, flags) != 0){
    kfree(mem);
    return 0;
  }

  return (uint64)mem;
}