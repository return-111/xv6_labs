#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main()
{
    int fdp2c[2], fdc2p[2];
    char buf[20];
    pipe(fdp2c);
    pipe(fdc2p);
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork() error\n");
        exit(1);
    } else if (pid == 0) { //child
        read(fdp2c[0], buf, 20);
        fprintf(1, "%d: received %s\n", getpid(), buf);
        write(fdc2p[1], "pong", 4);
        close(fdc2p[0]), close(fdc2p[1]);
        close(fdp2c[0]), close(fdp2c[1]);
    }else {
        write(fdp2c[1], "ping", 4);
        read(fdc2p[0], buf, 20);
        fprintf(1, "%d: received %s\n", getpid(), buf);
        close(fdc2p[0]), close(fdc2p[1]);
        close(fdp2c[0]), close(fdp2c[1]);
    }
    // fprintf(1, "4: received ping\n3: received pong\n");
    exit(0);
}