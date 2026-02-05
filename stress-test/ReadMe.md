# Install build prerequisites (example for Debian/Ubuntu)
sudo apt-get update
sudo apt-get install -y build-essential linux-headers-$(uname -r)

# Build
make

# Load with defaults (one worker per online CPU, 100% duty)
sudo insmod cpu_stress.ko

# Load with parameters (e.g., 4 threads, 75% busy, 10 ms period, pin threads, enable FPU, stop after 30s)
sudo insmod cpu_stress.ko threads=4 duty=75 period_ms=10 affinity=1 fpu=1 duration_s=30

# Check logs
dmesg | tail -n 50

# Unload
sudo rmmod cpu_stress
