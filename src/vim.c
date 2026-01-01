#include <unistd.h>
#include <stdio.h>

int main(void)
{
    execlp("vim", "vim", (char *)NULL);

    /* Si execlp retourne, c’est une erreur */
    perror("execlp");
    return 1;
}
