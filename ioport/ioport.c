// ioport.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/io.h>

int main(int argc, char *argv[])
{
    unsigned long port;
    unsigned int value;

    if (argc != 2 && argc != 3) {
        printf("Usage:\n");
        printf("  %s <port>\n", argv[0]);
        printf("  %s <port> <value>\n", argv[0]);
        return 1;
    }

    port = strtoul(argv[1], NULL, 0);

    if (ioperm(port, 1, 1)) {
        perror("ioperm");
        return 1;
    }

    if (argc == 2) {
        value = inb(port);
        printf("Port 0x%lx = 0x%02x\n", port, value);
    } else {
        value = strtoul(argv[2], NULL, 0);

        outb(value & 0xff, port);

        printf("Wrote 0x%02x to port 0x%lx\n",
               value & 0xff, port);

        printf("Readback = 0x%02x\n",
               inb(port));
    }

    ioperm(port, 1, 0);
    return 0;
}
