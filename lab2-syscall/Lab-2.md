## lab2-syscall

###  System call tracing

实验内容中的提示可以说是十分详细了，一步步照着做基本就没啥问题。

在记录进程相关信息的 proc 结构体中添加一个字段 tracemask，表示这个进程由 trace 系统调用创建的，这个进程调用相关的系统调用时可以依据 tracmask 来决定是否打印相关信息。

在 proc.h 中的 proc 结构体中添加字段 tracemask。

```c
int tracemask;
```

在 fork 时需要将父进程的 tracemask 也拷贝给子进程，在 fork 函数中添加下面的代码。

```c
// copy trace mask
np->tracemask = p->tracemask;
```

添加一个系统调用 trace，作用就是将传入的系统调用掩码拷贝至 proc 中的 tracmask 。

```c
uint64 sys_trace(void)
{
  int mask;

  if(argint(0, &mask) < 0)
    return -1;

  myproc()->tracemask = mask; 
  return 0;
}

```

接着就只需要在 syscall 中，在执行完相关的系统调用后添加一个判断语句，如果是 trace 系统调用指定的系统调用，则打印相关的调用信息。

```c
void syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
    if((1 << num) & p->tracemask) {
      printf("%d: syscall %s -> %d\n", p->pid, syscallnames[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}

```



###  Sysinfo

这一题把要做什么搞明白了之后，分解一下问题就会好做不少。

#### 整体的逻辑：

用户的系统调用会传入一个指向 sysinfo 的指针 ，在sysinfo系统调用中我们需要先创建一个sysinfo对象，然后填充到系统调用传入的指针中。

#### 第一个问题：怎样进行拷贝操作

使用copyout函数将内核中的sysinfo对象拷贝到指针指向的用户空间。

```c
uint64 sys_sysinfo(void)
{
  uint64 addr;
  struct proc *p = myproc();
  struct sysinfo info;

  if(argaddr(0, &addr) < 0)
    return -1;

  if(freemem(&info.freemem) < 0)
    return -1;

  if(nproc(&info.nproc) < 0)
    return -1;

  if(copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}
```

#### 第二个问题：怎样获取空闲内存大小

在内存控制相关的内核代码kalloc.c中编写一个函数，来获取空闲内存大小，在这个函数中遍历freelist，看共有几个空闲页，然后将页数乘上pagesize就算出空闲内存大小有多大了。

```c
int freemem(uint64* mem)
{
  uint64 n;
  struct run *r;
  r = kmem.freelist;
  for (n = 0; r != 0; r = r->next, n++);
  *mem = n * PGSIZE;

  return 0;
}
```

#### 第三个问题：怎样获取进程数

在进程管理相关的内核代码proc.c中编写一个函数，来获取系统中进程的总数，在这个函数中遍历进程表，然后判断该进程的状态是否是unused，如果不是就将统计量加一，遍历完整个进程表后就统计出了当前系统中存在的进程数目。

```c
int nproc(uint64* np)
{
  uint64 n = 0;
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state != UNUSED)
      n++;
  }

  *np = n;

  return 0;
}
```

