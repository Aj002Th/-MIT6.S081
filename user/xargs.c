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