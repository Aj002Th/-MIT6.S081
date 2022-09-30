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
