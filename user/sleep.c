#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(2, "sleep: sleep error arg missing\n");
        exit(1);
    }
    int tim = atoi(argv[1]);
    // fprintf(1, "%d", tim);
    sleep(tim);
    exit(0);
}