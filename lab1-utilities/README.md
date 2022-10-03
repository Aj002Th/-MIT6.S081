## lab1-utilities

### sleep

这题没啥东西，直接调用系统调用。

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
    int wait;

    if (argc < 2) {
        fprintf(2, "sleep: need a argument\n");
        exit(1);
    }

    wait = atoi(argv[1]);
    sleep(wait);

    exit(0);
}
```



### pingpong

创建两个管道来实现父子进程之间的双向通信。

需要注意的地方可能就是要及时关闭不用的文件描述符吧。

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
    int f2s[2];
    int s2f[2];
    void* data = malloc(1);

    pipe(f2s);
    pipe(s2f);

    if (argc > 1) {
        fprintf(2, "pingpong: too many argument\n");
        exit(1);
    }

    if (fork() == 0) {
        // son
        close(f2s[1]);
        close(s2f[0]);
        read(f2s[0], data, 1);
        close(f2s[0]);
        printf("%d: received ping\n", getpid());
        write(s2f[1], data, 1);
        close(s2f[1]);
    } else {
        // father
        close(f2s[0]);
        close(s2f[1]);
        write(f2s[1], malloc(1), 1);
        close(f2s[1]);
        read(s2f[0], data, 1);
        close(s2f[0]);
        printf("%d: received pong\n", getpid());
    }

    exit(0);
}
```



### primes

这题用了埃氏筛的算法，还是个多进程版本。

需要注意的是管道close 的时机，如果没有及时关闭管道会导致子进程的read阻塞，然后就卡住了。

埃氏筛的实现中提到每个进程都从父进程收一些数，经过过滤后将剩余的数又接着传递到子进程，这种行为模式很自然想到使用递归。

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void handle(int p[2]) {
    int num = 0;
    int prime = -1;
    int cnt = 0;
    int newp[2];
    
    pipe(newp);

    while (read(p[0], &num, 4)) {
        if (num == 0)
            break;

        if (prime == -1) {
            printf("prime %d\n", num);
            prime = num;
        } else {
            if(num % prime) {
                write(newp[1], &num, 4);
                cnt++;
            }
        }
    }

    close(p[0]);
    close(newp[1]); // 在fork前要关, 不然子进程就卡在上read了

    if (cnt) {
        if (fork() == 0) {
            handle(newp);
            exit(1);
        }
    }

    wait(0);

    return;
}

int main(int argc, char* argv[]) {
    if(argc > 1){
        fprintf(2, "primes: do not need any argument\n");
        exit(0);
    }

    int p[2];
    pipe(p);

    for (int i = 2; i <= 35; i++) {
        write(p[1], &i, 4);
    }

    close(p[1]);
    handle(p);
    close(p[0]);

    exit(0);
}
```



### find

这个题主要也是模仿，依据提示查看 ls 的实现之后在 ls 的实现代码上弄明白如何遍历的文件夹，适当修改就能完成 find 的任务。

由于我们需要递归地查找子目录，所以 find 肯定也得用递归来实现。

```c
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

void find(char* path, char* filename) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_FILE:
            fprintf(2, "find: first parameter cannot be file, must be a dir\n");
            break;

        case T_DIR:
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("ls: path too long\n");
                break;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';

            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                    continue;
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                if (stat(buf, &st) < 0) {
                    printf("find: cannot stat %s\n", buf);
                    continue;
                }

                if (strcmp(de.name, filename) == 0) {
                    printf("%s\n", buf);
                }

                if (st.type == T_DIR) {
                    find(buf, filename);
                }
            }
            break;
    }
    close(fd);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(2, "find: need and only need 2 parameter\n");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}
```



### xargs

这题其实感觉和第二题差别都不是很大，就是要理解一下管道是怎么优雅的实现 io 重定向的、xargs所作的工作具体是什么。

管道将上一命令的标准输出替换为下一命令的标准输入，以此实现的重定向。

但是 main 函数的参数是从 argv 里获取，所以需要命令有一些额外的处理，将标准输入中的内容也放入 argv 中，这就是 xargs 所做的事情。

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char* argv[]) {
    char* xargv[MAXARG];
    char c;
    char* p = 0;
    char buf[512];
    int n = 1;

    if (argc < 2) {
        fprintf(2, "xargs: no command\n");
        exit(1);
    }

    memset(buf, 0, sizeof(buf));
    memset(xargv, 0, sizeof(xargv));
    for (int i = 1; i < argc; i++) {
        xargv[i-1] = argv[i];
    }

    while (n) {
        p = buf;
        while ((n = read(0, &c, 1)) && c != '\n') {
           *p = c; 
           ++p;
        }
        *p = '\0';

        if(p != buf) {
            xargv[argc-1] = buf;
            if (fork() == 0) {
                exec(argv[1], xargv);
                exit(1);
            }
            wait(0);
        }

        if(n == 0) {
            break;
        }
    }
    
    exit(0);
}
```

