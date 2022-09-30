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