// msi_handler.c
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#define DRV_NAME "msi_handler"

/* Replace with your real VID:DID */
#define PCI_VENDOR_ID 0x1022
#define PCI_DEVICE_ID 0x17D8

static irqreturn_t msi_irq_handler(int irq, void *dev_id)
{
    struct pci_dev *pdev = dev_id;

    /*
     * IMPORTANT:
     * 1. Read device interrupt status register
     * 2. Clear/ACK interrupt at device
     *
     * Example (pseudo):
     *   status = readl(bar + INT_STATUS);
     *   writel(status, bar + INT_STATUS);
     */

    pr_info(DRV_NAME ": MSI interrupt received (irq=%d)\n", irq);

    return IRQ_HANDLED;
}

static int msi_probe(struct pci_dev *pdev,
                    const struct pci_device_id *id)
{
    int ret;

    ret = pci_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    /* Enable MSI */
    ret = pci_enable_msi(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable MSI\n");
        goto err_disable;
    }

    ret = request_irq(pdev->irq, msi_irq_handler,
                      0, "my_msi", pdev);
    if (ret) {
        pci_disable_msi(pdev);
		dev_err(&pdev->dev, "irq request failed\n");
        goto err_disable;
    }

    return 0;

err_disable:
    pci_disable_device(pdev);
    return ret;
}


static void msi_remove(struct pci_dev *pdev)
{
    free_irq(pdev->irq, pdev);
    pci_disable_msi(pdev);
    pci_disable_device(pdev);

    dev_info(&pdev->dev, "Removed\n");
}

static const struct pci_device_id msi_pci_ids[] = {
    { PCI_DEVICE(PCI_VENDOR_ID,
                  PCI_DEVICE_ID) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, msi_pci_ids);

static struct pci_driver msi_pci_driver = {
    .name     = DRV_NAME,
    .id_table = msi_pci_ids,
    .probe    = msi_probe,
    .remove   = msi_remove,
};

module_pci_driver(msi_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Minimal PCI MSI handler driver");