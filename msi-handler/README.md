# msi_handler

Minimal Linux PCI MSI handler

## Build
make

## Load
sudo insmod msi_handler.ko

## Unload
sudo rmmod msi_handler

## Debug
dmesg | tail
cat /proc/interrupts
lspci -vv -s <BDF>