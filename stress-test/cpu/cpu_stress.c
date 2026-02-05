// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/sched/signal.h>
#include <linux/cpumask.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/preempt.h>
#include <linux/version.h>
#include <asm/fpu/api.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dhinesh Thangamani");
MODULE_DESCRIPTION("CPU stressor kernel module using basic arithmetic");
MODULE_VERSION("1.0");

/*
 * Module parameters:
 *  - threads: number of worker threads (default: # of online CPUs)
 *  - affinity: pin one thread per CPU (best effort)
 *  - duty: busy percentage [1..100] in each period_ms
 *  - period_ms: scheduling period for duty cycle
 *  - fpu: enable floating-point math (uses kernel_fpu_* APIs)
 *  - nice: niceness for worker threads (-20..19), applied if possible
 *  - duration_s: auto-stop after N seconds (0 = run until rmmod)
 */

static unsigned int threads;
module_param(threads, uint, 0444);
MODULE_PARM_DESC(threads, "Number of worker threads (default = online CPUs)");

static bool affinity = true;
module_param(affinity, bool, 0444);
MODULE_PARM_DESC(affinity, "Pin workers to distinct CPUs (default = true)");

static unsigned int duty = 100;
module_param(duty, uint, 0644);
MODULE_PARM_DESC(duty, "Busy duty cycle percentage [1..100] (default = 100)");

static unsigned int period_ms = 10;
module_param(period_ms, uint, 0644);
MODULE_PARM_DESC(period_ms, "Duty period in milliseconds (default = 10 ms)");

static bool fpu = false;
module_param(fpu, bool, 0644);
MODULE_PARM_DESC(fpu, "Perform floating-point ops (default = false)");

static int nice = 0;
module_param(nice, int, 0644);
MODULE_PARM_DESC(nice, "Thread niceness (-20..19, default = 0)");

static unsigned int duration_s = 0;
module_param(duration_s, uint, 0444);
MODULE_PARM_DESC(duration_s, "Auto-stop after N seconds (0 = until rmmod)");

struct stress_worker {
    struct task_struct *task;
    int cpu_target;        /* -1 if not pinned */
    bool running;
};

static struct stress_worker *workers;
static unsigned int worker_count;
static ktime_t start_kt;

/* Simple integer arithmetic workload (volatile to prevent over-optimization) */
static __always_inline void do_int_ops(unsigned long iters)
{
    volatile u64 a = 0x9e3779b97f4a7c15ULL; /* golden ratio constant */
    volatile u64 b = 0xC2B2AE3D27D4EB4FULL; /* random-ish */
    volatile u64 c = jiffies;

    while (iters--) {
        a += b;
        b ^= a;
        c = (c + a) * 33u + (b >> 3);
        a = (a << 5) | (a >> 59);
        b = (b << 7) | (b >> 57);
        c ^= (a ^ b);
    }
    /* Prevent the compiler from discarding results entirely */
    barrier();
}

/* Optional floating-point workload (guarded by kernel_fpu_begin/end) */
static __always_inline void do_fpu_ops(unsigned long iters)
{
#ifdef CONFIG_KERNEL_FPU
    kernel_fpu_begin();
    volatile double x = 1.000001, y = 0.999999, z = 3.1415926535;
    while (iters--) {
        x = x * 1.0000001 + y * 0.9999997;
        y = y * 0.9999993 + z * 1.0000002;
        z = z * 0.9999999 + x * 0.0000001;
    }
    kernel_fpu_end();
#else
    /* If FPU not available in kernel context, fall back to integer ops */
    do_int_ops(iters);
#endif
}

/* One worker thread */
static int stress_fn(void *data)
{
    struct stress_worker *w = data;
    const u64 period_ns = (u64)period_ms * 1000000ULL;
    u64 busy_ns = div_u64(period_ns * duty, 100);
    ktime_t t0, now;

    /* Apply nice value (best effort for kernel thread) */
    set_user_nice(current, clamp_val(nice, -20, 19));

    /* Pin to CPU if requested and available */
    if (w->cpu_target >= 0) {
        struct cpumask mask;
        cpumask_clear(&mask);
        cpumask_set_cpu(w->cpu_target, &mask);
        set_cpus_allowed_ptr(current, &mask);
    }

    w->running = true;
    pr_info("cpu_stress: worker on CPU%d started (duty=%u%% period=%ums fpu=%d)\n",
        raw_smp_processor_id(), duty, period_ms, fpu);

    while (!kthread_should_stop()) {
        t0 = ktime_get();
        /* Busy section */
        do {
            /* Tune iterations so each loop does a small amount of work */
            do_int_ops(1024);
            if (fpu)
                do_fpu_ops(256);

            /* Be scheduler-friendly; prevents soft lockups at 100% */
            cond_resched();

            now = ktime_get();
        } while (ktime_to_ns(ktime_sub(now, t0)) < busy_ns && !kthread_should_stop());

        /* Sleep for the remainder of the period (if any) */
        if (busy_ns < period_ns) {
            u64 rem_ns = period_ns - busy_ns;
            /* Convert to jiffies; sleep at least 1 jiffy if rem_ns > 0 */
            u64 rem_ms = rem_ns / 1000000ULL;
            if (rem_ms == 0)
                rem_ms = 1;
            if (msleep_interruptible(rem_ms) && kthread_should_stop())
                break;
        }

        /* Optional timed stop */
        if (duration_s) {
            ktime_t elapsed = ktime_sub(ktime_get(), start_kt);
            if (ktime_to_ms(elapsed) >= (u64)duration_s * 1000ULL)
                break;
        }
    }

    w->running = false;
    pr_info("cpu_stress: worker on CPU%d exiting\n", raw_smp_processor_id());
    return 0;
}

static int __init cpu_stress_init(void)
{
    unsigned int i;
    unsigned int online = num_online_cpus();    // Gets the total number cpus available in this system
    unsigned int next_cpu = 0;

    if (!threads || threads > online)
        worker_count = online;
    else
        worker_count = threads;

    if (duty == 0) duty = 1;
    if (duty > 100) duty = 100;
    if (period_ms == 0) period_ms = 1;

    workers = kcalloc(worker_count, sizeof(*workers), GFP_KERNEL);
    if (!workers)
        return -ENOMEM;

    start_kt = ktime_get();

    for (i = 0; i < worker_count; i++) {
        int cpu_target = -1;

        if (affinity) {
            /* Spread workers across online CPUs */
            while (!cpu_online(next_cpu))
                next_cpu = (next_cpu + 1) % nr_cpu_ids;
            cpu_target = next_cpu;
            next_cpu = cpumask_next(next_cpu, cpu_online_mask);
            if (next_cpu >= nr_cpu_ids)
                next_cpu = cpumask_first(cpu_online_mask);
        }

        workers[i].cpu_target = cpu_target;
        workers[i].task = kthread_run(stress_fn, &workers[i], "cpu_stress/%u", i);
        if (IS_ERR(workers[i].task)) {
            int ret = PTR_ERR(workers[i].task);
            workers[i].task = NULL;
            pr_err("cpu_stress: failed to start worker %u (err=%d)\n", i, ret);
            /* Best effort: stop already-started workers */
            while (i--) {
                if (workers[i].task)
                    kthread_stop(workers[i].task);
            }
            kfree(workers);
            return ret;
        }
    }

    pr_info("cpu_stress: started %u worker(s); duty=%u%% period=%ums affinity=%d fpu=%d duration=%us\n",
        worker_count, duty, period_ms, affinity, fpu, duration_s);
    return 0;
}

static void __exit cpu_stress_exit(void)
{
    unsigned int i;
    if (!workers)
        return;

    for (i = 0; i < worker_count; i++) {
        if (workers[i].task) {
            kthread_stop(workers[i].task);
            workers[i].task = NULL;
        }
    }
    kfree(workers);
    pr_info("cpu_stress: module unloaded\n");
}

module_init(cpu_stress_init);
module_exit(cpu_stress_exit);
