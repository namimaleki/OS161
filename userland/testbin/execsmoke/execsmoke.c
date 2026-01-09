#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int main(void) {
    /* string literal needs a cast to drop const for execv’s argv */
    char *const av[] = { (char *)"true", NULL };

    printf("before exec\n");
    int r = execv("/bin/true", av);   /* or "/testbin/argtest" */
    printf("execv returned %d errno %d\n", r, errno); /* only prints if exec fails */
    return 1;
}
