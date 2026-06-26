#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/io.h>

int main(int argc, char *argv[])
{
    unsigned int smi_val;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <smi_value>\n", argv[0]);
        fprintf(stderr, "Example: %s 0x99\n", argv[0]);
        return 1;
    }

    smi_val = strtoul(argv[1], NULL, 0);

    if (smi_val > 0xFF) {
        fprintf(stderr, "Error: SMI value must be 0x00-0xFF\n");
        return 1;
    }

    if (ioperm(0xB2, 1, 1)) {
        perror("ioperm");
        return 1;
    }

    printf("Writing SW SMI 0x%02X to port 0xB2\n", smi_val);

    outb((unsigned char)smi_val, 0xB2);

    return 0;
}
