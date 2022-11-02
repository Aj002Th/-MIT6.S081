## lab5-COW

### Implement copy-on write

这个实验要写的代码还挺多的，我感觉还是挺难的，容易出小问题出。

首先需要明白 COW 是在干嘛，它的主要的想法是懒加载，在执行 fork 时先不对子进程分配新的物理内存，而是将子进程的虚拟页映射到父进程的物理页上，父子进程共享同一只读物理页，以此减少拷贝操作和物理页分配的操作。当等到父进程或者子进程需要对共享页进行写操作是，为了保证进程间的隔离，这个时候才分配一个新的物理页，将共享页中的内容拷贝过来，给这个进程用来修改。

第一步是需要在页表项上找一位来标记这一页是 COW 页。依据risc的手册可以知道，页表项的8、9、10三位是给程序保留使用的位，我们随意选择一位来进行标记即可。

```c
// riscv.h
#define PTE_COW (1L << 8) // 1 -> RSW
```

然后我们需要修改uvmcopy()函数，因为 fork 就是调用该函数来为子进程分配物理空间并进行虚拟地址映射的。我们让这个函数不进行物理内存的申请和内容的拷贝，而是简单地将子进程的页表项中的虚拟地址映射到父进程的物理地址上。然后我们需要将父子进程的对应的页表项设置为COW 页，并且是只读权限。这里不要忘记增加物理页的引用计数（这一点需要结合后文）。

```c
// vm.c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");

    // 清除 PTE_W 位
    *pte &= ~PTE_W;
    // 设置 PTE_COW 位
    *pte |= PTE_COW; 

    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    increaseRefNum(pa);

    // 不实际分配内存, 将父进程的物理地址映射过去
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

由于COW页表项都设置为了只读权限，所以当进程在使用对应的页时就会触发中断，我们下一步就需要在usertrap()中捕获到因为 COW 页而出现的缺页中断。查找 risc-v 手册可以知道，缺页中断的中断号为12、13、15，分别对应由于 读、写、指令 引发的缺页中断，中断号可以从 scase 寄存器中取得。拦截下这些缺页故障中断后判断其是否为 COW 页，如果是COW页则需要进行故障恢复：为这一虚拟页分配新的物理页，并将原来所指向的物理页中的内容拷贝过来。

```c
// trap.c -> usertrap
else if(r_scause() == 12 || r_scause() == 13 || r_scause() == 15){
    // 由于 读、写、指令 引发的缺页中断
    uint64 va = r_stval(); // 请求哪个虚拟页导致的中断
    if(va > p->sz || isCOW(walk(p->pagetable, va, 0)) == 0 || cowKalloc(p->pagetable, va) == 0) {
      p->killed = 1;
    }
```

在 copyout 里，如果写入的虚拟页是 COW 页，则进行同样的处理。

```c
// vm.c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    
    // cow 页, 要分配内存
    pte_t *pte = walk(pagetable, va0, 0);
    if (isCOW(pte) == 1) {
      pa0 = cowKalloc(pagetable,va0);
      if(pa0 == 0){
        return -1;
      }
    } 
    
    // 正常抄数据
    memmove((void *)(pa0 + (dstva - va0)), src, n);
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

这里对 COW 页进行内存分配的具体操作封装在了 cowKalloc 函数中。

在讨论这个函数的实现之前，还需要了解一下由于COW的机制又会带来一些新的问题。COW页可能同时被多个进程所引用，所以一个进程释放自己的内存空间时如果不加判断，可能会释放掉其他进程还在使用的 COW 页，所以我们需要建立一个数组，记录每个物理页被几个进程所使用，在kalloc时将引用初始为1，在uvmcopy()中创建新的cow页时对引用数做加法操作，kfree里对引用数做减法操作，只有当一个物理页的引用数为1，才能真正释放这一页的物理内存。

首先是在 kmem 中添加一个记录每个物理页引用数的数组。

```c
// kalloc.c
struct {
  struct spinlock lock;
  struct run *freelist;
  int refNum[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;
```

接着添加一些对于 refNum 数组的一些工具函数，包括增加计数、获取计数等。

```c
// kalloc.c
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
```

同样要回顾一下之前的一些实现中，是否需要对引用计数进行修改。uvmcopy 中在给子进程虚拟页映射父进程的物理页时需要增加引用计数、kfree 的工作变成减少物理页的引用计数，只有在引用计数为 0 时，才真正回收物理页、kalloc 需要将刚分配的物理页引用计数初始化为 1。

```c
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
```

``` c
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
```

这里有个比较坑的地方就是 freerange 在启动时回收所有可分配的内存空间，这里会调用 kfree，由于我们在 kfree 里设置的是减 1 后为 0 才能回收，所以需要初始化这里为 1。

```c
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    acquire(&kmem.lock);
    kmem.refNum[((uint64)p - KERNBASE) / PGSIZE] = 1; // 小坑点
    release(&kmem.lock);
    kfree(p);
  }
}
```

这时就能开始实现 cowKalloc 函数了，首先因为物理内存是一页一页分配的，所以通过PGROUNDDOWN 先获取到出现缺页的虚拟地址所在虚拟页的起始地址，再通过 pte 取得物理地址。有个特殊情况，就是如果该物理页的引用计数为 1 ，就说明这物理页没有被其他人共享，所以直接取消 COW 位，添加写权限就好。其他的情况就像原来的 uvmcopy 里的处理一样，申请新的物理页，拷贝内容，进行映射。这里又有个小点，就是需要先让全本的页表项无效，不然 mappages 会因为重映射而失败。


```c
// kalloc.c
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
```

暴露一下声明就大功告成了。


```c
// defs.h
int             increaseRefNum(uint64);
int             decreaseRefNum(uint64);
int             isCOW(pte_t*);
int             getRefNum(uint64);
uint64          cowKalloc(pagetable_t, uint64);

pte_t*         walk(pagetable_t pagetable, uint64 va, int alloc);
```

