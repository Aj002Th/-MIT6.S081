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