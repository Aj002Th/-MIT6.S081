### lab3-pgtbl

### Speed up system calls

需要理清楚思路，到底要我们干什么事情。

我们要实现的任务是加快系统调用的速度。怎么增加呢？题目中给到的办法是，创建一个内核和用户共享的页面，这个页用户只读，这样一来，使用系统调用获取某些内核中的信息这个操作就可以不需要再陷入内核，而是直接读取相应页面中的数据即可了。

所有的工作其实都是围绕如何在 USYSCALL 这个指定的虚拟地址页中存放好 pid 的信息。

阅读了相关的代码之后会发现，我们所需要实现的这个 usyscall 在很多的逻辑上其实和已经实现好的 trapframe 是类似的，可以模仿着写。

模仿 trapframe 的实现，在proc 结构体中添加 usyscall 指针

```c
  struct usyscall *usyscall; // data page for usyscall
```

allocproc中，创建一个新进程时，需要为 usyscall 结构体分配物理空间

```c
  // Allocate a usyscall page.
  if((p->usyscall = (struct usyscall *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  // copy the pid
  p->usyscall->pid = p->pid;
```

freeproc中，销毁进程结构是要释放掉分配给 usyscall 的物理页

```c
if(p->usyscall)
    kfree((void*)p->usyscall);
  p->usyscall = 0;
```

proc_pagetable中，需要将 proc 结构体中的 usyscall 结构所使用的物理页映射到到设定好的 USYSCALL 虚拟页上

```c
// map the usyscall struct to getpid()
  if(mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }
```

proc_freepagetable中，需要解除掉页表中的映射关系

```c
uvmunmap(pagetable, USYSCALL, 1, 0);
```



### Print a page table

这题需要理解页表是怎样的一个模型。

知道了页表的结构之后其实这题就非常简单的，就是一个递归打印 pte 信息。

这里有个坑就是，如果没有完成好上一题，这一题的输出会少一行（对应上一题中给 usyscall 分配的页表），就会导致这一题也不通过。

下面是需要添加到 vm.c 中的核心代码

```c
void
pteprint(pagetable_t pagetable, int level) {
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    uint64 child = PTE2PA(pte);

    // print
    if(pte & PTE_V) {
      for(int j = 0; j < level; j++){
        if (j) printf(" ");
        printf("..");
      }
      printf("%d: pte %p pa %p\n", i, pte, child);
    }

    // find child
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      pteprint((pagetable_t)child, level + 1);
    }
  }
}

// print page table
void
vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  pteprint(pagetable, 1);
}
```



### Detecting which pages have been accessed

这个题标记了个 hard，其实并没有很难。

查看 xv6 的手册之后就会发现 risc-V 页表的 access 位是第六位，所以 PTE_A 的位移就可以确定了。

PTE_A 位的置 1 操作硬件会帮我们完成，所以我们只需要考虑在检查之后对其进行清零即可。

先用 argaddr 和 argint 获取系统调用的参数，然后使用 walk 函数查页表来获得需要检查的页表项，然后用掩码判断一下 PTE_V 的情况，把结果记录在 bitmask 中，最后使用 copyout 拷贝到用户空间即可。

核心代码如下

```c
#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 page_addr = 0;
  int npage = 0;
  uint64 user_buffer = 0;
  uint64 bitmask = 0;

  pte_t *pte = 0;

  if(argaddr(0, &page_addr) < 0) return -1;
  if(argint(1, &npage) < 0) return -1;
  if(argaddr(2, &user_buffer) < 0) return -1;

  // limit n
  if(npage > 64) return -1;

  // walk page and set bitmask
  for(int i = 0; i < npage; i++) {
    pte = walk(myproc()->pagetable, page_addr, 0);
    if(pte == 0) return -1;

    // count and clear PTE_A bit
    if((*pte & PTE_V) && (*pte & PTE_A)) {
      bitmask = bitmask | (1 << i);
      (*pte) = (*pte) & (~PTE_A);
    }

    // add page_addr
    page_addr += PGSIZE;
  }

  // copy bitmask to user buffer
  if(copyout(myproc()->pagetable, user_buffer, (char*)&bitmask, sizeof(bitmask)) < 0)
    return -1;

  return 0;
}
#endif
```

