// gpe_rate.c
// Diagnostic kernel module: per-second rate for each ACPI GPE using
// /sys/firmware/acpi/interrupts/gpeXX counters.
//
// NOTE: This module reads sysfs from kernel context (diagnostic use).
// Prefer user-space tooling for production, but this satisfies the
// "driver" requirement and keeps ACPI handling non-invasive.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

#define DRV_NAME "gpe_rate"
#define ACPI_INTR_DIR "/sys/firmware/acpi/interrupts"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dhinesh Thangamani");
MODULE_DESCRIPTION("Count ACPI GPE events per second (each GPE separately)");
MODULE_VERSION("1.0");

static unsigned int interval_ms = 1000;
module_param(interval_ms, uint, 0444);
MODULE_PARM_DESC(interval_ms, "Sampling interval in ms (default 1000)");

struct gpe_entry {
    struct list_head list;
    char name[32];          // e.g. "gpe0A"
    char path[256];         // full path to sysfs file
    u64 last_count;         // previous total
    u64 rate;               // delta per interval
    bool valid;
};

static LIST_HEAD(gpe_list);
static DEFINE_MUTEX(gpe_lock);

static struct dentry *dbg_dir;
static struct delayed_work sample_work;

static bool is_gpe_filename(const char *s, int len)
{
    // Accept gpe + 2 hex digits exactly: gpe00..gpeFF
    // Exclude "gpe_all" and other non-per-GPE entries.
    if (len != 5) return false;
    if (s[0] != 'g' || s[1] != 'p' || s[2] != 'e') return false;
    return isxdigit(s[3]) && isxdigit(s[4]);
}

static int read_u64_from_file(const char *path, u64 *val)
{
    struct file *f;
    char buf[64];
    loff_t pos = 0;
    ssize_t n;
    int ret;

    f = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(f))
        return PTR_ERR(f);

    n = kernel_read(f, buf, sizeof(buf) - 1, &pos);
    filp_close(f, NULL);

    if (n <= 0)
        return (n == 0) ? -EIO : (int)n;

    buf[n] = '\0';

    // sysfs file often looks like: "1084 enable\n" or "1084  enable\n"
    // Parse the first token as u64.
    {
        char *p = buf;
        char *end;

        while (*p && isspace(*p)) p++;
        end = p;
        while (*end && !isspace(*end)) end++;
        *end = '\0';

        ret = kstrtoull(p, 10, val);
        if (ret)
            return ret;
    }

    return 0;
}

/* --- Directory enumeration using iterate_dir() --- */

struct enum_ctx {
    struct dir_context ctx;
};

static int enum_actor(struct dir_context *ctx, const char *name, int namlen,
              loff_t offset, u64 ino, unsigned int d_type)
{
    struct gpe_entry *e;

    if (!is_gpe_filename(name, namlen))
        return 0;

    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e)
        return -ENOMEM;

    snprintf(e->name, sizeof(e->name), "%.*s", namlen, name);
    snprintf(e->path, sizeof(e->path), "%s/%s", ACPI_INTR_DIR, e->name);
    e->last_count = 0;
    e->rate = 0;
    e->valid = true;

    mutex_lock(&gpe_lock);
    list_add_tail(&e->list, &gpe_list);
    mutex_unlock(&gpe_lock);

    return 0;
}

static int enumerate_gpes(void)
{
    struct file *dir;
    struct enum_ctx ectx;
    int ret = 0;

    dir = filp_open(ACPI_INTR_DIR, O_RDONLY | O_DIRECTORY, 0);
    if (IS_ERR(dir))
        return PTR_ERR(dir);

    memset(&ectx, 0, sizeof(ectx));
    ectx.ctx.actor = enum_actor;

    ret = iterate_dir(dir, &ectx.ctx);

    filp_close(dir, NULL);
    return ret;
}

static void free_gpes(void)
{
    struct gpe_entry *e, *tmp;

    mutex_lock(&gpe_lock);
    list_for_each_entry_safe(e, tmp, &gpe_list, list) {
        list_del(&e->list);
        kfree(e);
    }
    mutex_unlock(&gpe_lock);
}

/* --- Periodic sampling --- */

static void sample_once(void)
{
    struct gpe_entry *e;
    u64 cur = 0;

    mutex_lock(&gpe_lock);
    list_for_each_entry(e, &gpe_list, list) {
        if (!e->valid)
            continue;

        if (read_u64_from_file(e->path, &cur) == 0) {
            // delta per interval; for 1000ms this is per-second.
            e->rate = (cur >= e->last_count) ? (cur - e->last_count) : 0;
            e->last_count = cur;
        } else {
            e->valid = false;
            e->rate = 0;
        }
    }
    mutex_unlock(&gpe_lock);
}

static void sample_work_fn(struct work_struct *work)
{
    sample_once();
    schedule_delayed_work(&sample_work, msecs_to_jiffies(interval_ms));
}

/* --- debugfs output --- */

static int gpe_rates_show(struct seq_file *s, void *unused)
{
    struct gpe_entry *e;

    seq_printf(s, "interval_ms=%u\n", interval_ms);
    seq_puts(s, "name   rate_per_interval  total_count  status\n");

    mutex_lock(&gpe_lock);
    list_for_each_entry(e, &gpe_list, list) {
        seq_printf(s, "%s   %-17llu  %-11llu  %s\n",
               e->name,
               (unsigned long long)e->rate,
               (unsigned long long)e->last_count,
               e->valid ? "ok" : "read_fail");
    }
    mutex_unlock(&gpe_lock);

    return 0;
}

static int gpe_rates_open(struct inode *inode, struct file *file)
{
    return single_open(file, gpe_rates_show, inode->i_private);
}

static const struct file_operations gpe_rates_fops = {
    .owner   = THIS_MODULE,
    .open    = gpe_rates_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static int __init gpe_rate_init(void)
{
    int ret;

    pr_info(DRV_NAME ": init (interval_ms=%u)\n", interval_ms);

    ret = enumerate_gpes();
    if (ret < 0) {
        pr_err(DRV_NAME ": enumerate_gpes failed: %d\n", ret);
        return ret;
    }

    // Prime initial counts so first interval is meaningful.
    sample_once();

    dbg_dir = debugfs_create_dir("gpe_rate", NULL);
    if (!dbg_dir) {
        pr_err(DRV_NAME ": debugfs_create_dir failed\n");
        free_gpes();
        return -ENOMEM;
    }

    if (!debugfs_create_file("gpe_rates", 0444, dbg_dir, NULL, &gpe_rates_fops)) {
        pr_err(DRV_NAME ": debugfs_create_file failed\n");
        debugfs_remove_recursive(dbg_dir);
        free_gpes();
        return -ENOMEM;
    }

    INIT_DELAYED_WORK(&sample_work, sample_work_fn);
    schedule_delayed_work(&sample_work, msecs_to_jiffies(interval_ms));

    pr_info(DRV_NAME ": loaded. Read: /sys/kernel/debug/gpe_rate/gpe_rates\n");
    return 0;
}

static void __exit gpe_rate_exit(void)
{
    cancel_delayed_work_sync(&sample_work);
    debugfs_remove_recursive(dbg_dir);
    free_gpes();
    pr_info(DRV_NAME ": exit\n");
}

module_init(gpe_rate_init);
module_exit(gpe_rate_exit);

