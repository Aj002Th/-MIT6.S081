## lab4-traps

### RISC-V assembly

1. Which registers contain arguments to functions? For example, which register holds 13 in main's call to `printf`?

    a0 到 a7 寄存器可以存八个参数，main 中的 13 是第三个参数存在 a2 中

2. Where is the call to function `f` in the assembly code for main? Where is the call to `g`? (Hint: the compiler may inline functions.)

    在 c 代码里，main 调用了 f ，然后 f 调用了 g 。但是实际在汇编代码中，汇编器进行了一个内联的优化，直接将 f(8)+1 的值提前算好了，main 中直接使用了结果 12。

3. At what address is the function `printf` located?

    ```assembly
    30:	00000097          	auipc	ra,0x0
    34:	600080e7          	jalr	1536(ra) # 630 <printf>
    ```

    auipc 的作用是将立即数的值左移 12 位后加 PC ，将结果存储在寄存器中，立即数为 0 时其实就相当于取 pc 的值

    这里的 1536 是个 16进制数，其实是 0x600，加上 ra 中存储的 0x30，结果为 0x630

4. What value is in the register `ra` just after the `jalr` to `printf` in `main`?

    jalr 指令将返回后下一条需要执行的地址放入 ra，即 0x34 + 4 = 0x38

5. Run the following code.
    ```c
        unsigned int i = 0x00646c72;
        printf("H%x Wo%s", 57616, &i);
    ```
    What is the output? [Here's an ASCII table](http://web.cs.mun.ca/~michael/c/ascii-table.html) that maps bytes to characters.
    The output depends on that fact that the RISC-V is little-endian. If the RISC-V were instead big-endian what would you set `i` to in order to yield the same output? Would you need to change `57616` to a different value?
    [Here's a description of little- and big-endian](http://www.webopedia.com/TERM/b/big_endian.html) and [a more whimsical description](http://www.networksorcery.com/enp/ien/ien137.txt).

    57616=0xE110，所以前半部分是 HE110 ；0x00646c72 小端序要反过来变成 72-6c-64-00，对应的 ASCII 码 72:r 6c:l 64:d 00:字符串结束符，所以后半部分为 World；输出为 HE110 World
    
    如果变成大端序，因为57616 是直接输入的数字所以不用变化，i 需要反过来，变成 0x72-6c6400
    
6. In the following code, what is going to be printed after `'y='`? (note: the answer is not a specific value.) Why does this happen?

   ```
        printf("x=%d y=%d", 3);
   ```
   
    这里参数是从对应的寄存器中取的，如果代码能正常执行的话，y 的值就取决于原本 a2 中的内容



### Backtrace

主要是要理解在内核 trap 的过程（其实就是函数调用的过程）中 栈帧 是怎么分配和串联起来的

返回地址位于栈帧帧指针的固定偏移 -8 位置，并且保存的帧指针位于帧指针的固定偏移 -16 位置。先使用`r_fp()` 可以读取当前的帧指针，然后读出返回地址并打印，再将`fp`定位到前一个帧指针的位置继续读取即可。

依据提示，循环的终止条件可以通过 `PGROUNDUP(fp)` 和 `PGROUNDDOWN(fp)` 来判断，因为栈帧一次分配一页的大小，可以利用这一点来判断 fp 是否为一个合法的栈帧指针。

```c
void
backtrace(void)
{
  uint64 fp = r_fp();
  while(PGROUNDUP(fp) - PGROUNDDOWN(fp) == PGSIZE) {
    printf("%p\n", *(uint64*)(fp - 8));
    fp = *(uint64*)(fp - 16);
  }
}
```



### Alarm 

按照提示，第一步是要能成功调用用户空间里的函数，第二步是要保证调用完用户空间的函数后能够正确返回。

Sigalarm系统调用中的第一个参数是触发回调函数的时钟间隔，第二个参数是回调函数指针，这两个参数传入是通过 a0和a1寄存器实现的。这两个参数需要新创建两个字段保存在 proc 结构体中，系统调用 sys_sigalarm 的作用就是设置这两个字段。

```c
uint64 sys_sigalarm(void)
{
  if(argint(0, &myproc()->ticks) < 0)
    return -1;

  if(argaddr(1, (uint64*)&myproc()->alarmHandler) < 0)
    return -1;
  
  return 0;
}
```

函数指针指向的是用户空间的地址，在内核的代码中不能直接强转成函数指针然后直接调用。在进入trap前会将原本的寄存器保存在进程结构体的trapframe结构中，在返回 trap 时会将trapframe 中保存的寄存器复原。其中 trapframe->epc 里存储的就是中断返回后pc的值，所以只需要将回调函数的入口地址保存在trapframe->epc中，就能实现中断返回的时候调用设置的回调函数了。

但是还有一个问题就是回调函数有可能会修改trapframe中保存的值，这样的话最后回调函数执行结束后恢复到用户态时，状态就和trap时不一样了，这有悖于trap的初衷，所以需要在进入回调函数之前再进行一次寄存器的保存。这里可以直接再复制一个trapframe，执行完回调后将整个trapframe恢复回去即可。

还有一个问题就是如果在 alarm 的过程中又通过了多个时间间隔导致上一个alarm 还没执行完就要执行下一个 alarm 的情况（多进程并发也会造成同样的效果），这种情况下会导致备份的 trapframe 出现冲突，前一次备份的内容被覆盖，所以需要设置一个 isAlarming 标志，如果上一个 alarm回调没有执行完，那么将会暂时忽略下一次的 alarm。

综合上面几点，我们需要在 usertrap 中进行编码。我们需要判断此次中断是否为计时器中断，如果是计时器中断就将 proc 中的间隔统计量 passTicks 加 1，如果 ticks 等于 passTicks 说明到了设置的间隔时间，先将标志位 isAlarming 置 1 拒绝其他进程进入 alarm 代码，然后保存整个 trapframe 、清零passTicks，最后将 epc 设置为回调函数的入口，中断结束返回用户空间后 cpu 就会从回调函数开始执行。

```c
// trap.c -> usertrap
  if(which_dev == 2) {
    // 累计间隔
    p->passTicks += 1;

    // 调用 handler 发出 alarm
    if (p->ticks == p->passTicks && p->isAlarming == 0) {
      // 保存寄存器
      p->isAlarming = 1;
      // p->alarmTrapframe = p->trapframe;
      memmove(p->alarmTrapframe, p->trapframe, sizeof(struct trapframe));

      // 修改计数和返回地址
      p->passTicks = 0;
      p->trapframe->epc = (uint64)p->alarmHandler;
    }

    // 让出 cpu
    yield();
  }

```

那我们怎么能让回调函数运行结束后，程序从最开始进入中断之前的位置继续执行呢？这里我们的设计是要求回调函数的末尾必须使用 sys_sigreturn 系统调用。这个系统调用负责将 cpu 的状态恢复到执行回调函数之前的状态，我们在这个系统调用中需要做的就只是用 alarmTrapframe 替换掉 trapframe 即可。

```c
uint64 sys_sigreturn(void)
{
  // myproc()->trapframe = myproc()->alarmTrapframe;
  memmove(myproc()->trapframe, myproc()->alarmTrapframe, sizeof(struct trapframe));
  myproc()->isAlarming = 0;
  return 0;
}
```

需要在进程结构体里添加的字段就有：alarm间隔、已经经过的间隔、alarm回调函数、复制的备份trapframe、标志isAalrming。

```c
// proc.h -> proc 
int ticks;                  // alarm 的间隔
int passTicks;              // 已经经过的间隔
void(*alarmHandler)() ;    // alarm handler

int isAlarming;          // 进程是否正在执行 alarm
struct trapframe *alarmTrapframe;  // 保存执行 alarm 前的寄存器以恢复
```

