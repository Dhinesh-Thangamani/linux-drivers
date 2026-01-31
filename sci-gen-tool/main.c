/* *
 * Author: Dhinesh Thangamani
 *
 * License: General Public
 *
 * Purpose: Generates SCI, Handlers SCI Notification, Clear SCI Source
 *
 * */

#include <linux/module.h>   // core Linux kernel header for loadable kernel modules.
                            // Fns to create, manage and interact with KMs
#include <linux/init.h>     // init and exit up Fns
#include <linux/acpi.h>     // interface for ACPI related kernel Fns

#define DRV_NAME    "Acpi-L1C-Notify-Device-Driver"     // Driver Name used in this code

static const struct acpi_device_id l1c_acpi_ids [] = {  // List of ACPI device IDs to bind to this driver
    {"ACPI0017", 0},                                    // Change the device ID to bind the driver
    {"", 0},
};
MODULE_DEVICE_TABLE(acpi, l1c_acpi_ids);                // Exports the device IDs to user space
                                                        // KM should be auto-loaded for the given hardware

struct l1c_device {                                     // code ACPI Device structure
    struct acpi_device  *adev;                          // Provides access to _HID, _CID, _STA, etc.
};                                                      // device node described by ACPI FW (DSDT/SSDT tables)

static void l1c_notify_handler(
                                acpi_handle handle,     // handle Ref to ACPI namespace object
                                u32 event,              // ACPI events 0x80, 0x80, _PS0, etc
                                void *data              // context pointer from registration
                            ) {
    struct l1c_device *ldev = data;

    pr_info(DRV_NAME ": notify on %s: event=0x%x\n",
        dev_name(&ldev->adev->dev), event);
    pr_info(DRV_NAME "handler called\n");
}

static int l1c_add(struct acpi_device *adev) {
    acpi_status         status;
    struct l1c_device   *ldev;

    pr_info(DRV_NAME ": add for %s\n", dev_name(&adev-dev));

    ldev = devm_kzalloc(&adev->dev, sizeof(*ldev), GFP_KERNEL); // allocate device managed memory
    if (!ldev)
        return -ENOMEM;

    ldev->adev = adev;
    adev->driver_data = ldev;

    status = acpi_install_notify_handler(
                adev->handle,
                ACPI_DEVICE_NOTIFY,
                l1c_notify_handler,
                ldev
                );                                              // install acpi notify handler

    if (ACPI_FAILURE(status)) {
        pr_err(DRV_NAME ": failed to install notify handler, status=0x%x\n", status);
        return -EINVAL;
    }

    pr_info(DRV_NAME ": notify handler installed on %s\n",
                dev_name(&adev->dev));

    return 0;
}

static void l1c_remove(struct acpi_device *adev){
    acpi_status status;
    struct l1c_dev *ldev = adev->driver_data;

    pr_info(DRV_NAME ": remove for %s\n", dev_name(&adev->dev));
    
    if (!ldev)
        return;

    status = acpi_remove_notify_handler(
                adev->handle,
                ACPI_DEVICE_NOTIFY,
                l1c_notify_handler
                );                                          // remove acpi notify handler

    if (ACPI_FAILURE(status))
        pr_warn(DRV_NAME ": failed to remove notify handler, status=0x%x", status);
}

static struct acpi_driver l1c_acpi_driver = {               // acpi bus driver structure
    .name   = DRV_NAME,
    .class  = DRV_NAME,
    .ids    = l1c_acpi_ids,
    .ops    = {
        .add    = l1c_add;      // add function
        .remove = l1c_remove,   // remove function
    },
};

static void __init l1c_init(void) {
    pr_info(DRV_NAME ": init\n");
    return acpi_bus_register_driver(&l1c_acpi_driver);
}

static void __exit l1c_exit(void) {
    pr_info(DRV_NAME ": exit\n");
    acpi_bus_unregister_driver(&l1c_acpi_driver);
}

module_init(l1c_init);
module_exit(l1c_exit);

MODULE_AUTHOR("Dhinesh Thangamani")
MODULE_DESCRIPTION("ACPI Device Driver to Handler SCI Event")
MODULE_LICENSE("Personal")
