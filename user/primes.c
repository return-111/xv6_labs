#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void proc(int pre, int* fdl)
{
    fprintf(1, "prime %d\n", pre);
    int fdr[2], ff = 0;
    int x;
    close(fdl[1]);
    while (read(fdl[0], &x, sizeof (int))) {
        if (x % pre == 0) continue;
        if (ff) {
            write(fdr[1], &x, sizeof (int));
        } else {
            pipe(fdr);
            ff = 1;
            if (fork() == 0) {
                proc(x, fdr);
                exit(0);
            }else {
                close(fdr[0]);
            }
        }
    }
    if (ff) close(fdr[1]);
    close(fdl[0]);
    wait(0);
}


int main()
{
    int fds[2];
    pipe(fds);
    if (fork() == 0) {
        proc(2, fds);
        exit(0);
    }else {
        close(fds[0]);
        for (int i = 2; i <= 35; i++) {
            write(fds[1], &i, sizeof (int));
        }
        close(fds[1]);
    }
    wait(0);
    exit(0);
}