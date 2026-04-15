# Multi-Container Runtime with Kernel Monitoring

---

## 1. Team Information

- Pranavi Sistla - PES1UG24CS338
- Purav N Khincha - PES1UG24CS349

---

## 2. Build, Load, and Run Instructions

### Build project
make

### Load kernel module
sudo insmod monitor.ko

### Verify device
ls -l /dev/container_monitor

### Start supervisor
sudo ./engine supervisor ./rootfs-base

### Create container filesystems
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta

### Start containers
sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
sudo ./engine start beta ./rootfs-beta /bin/sh --soft-mib 64 --hard-mib 96

### List containers
sudo ./engine ps

### View logs
sudo ./engine logs alpha

### Stop containers
sudo ./engine stop alpha
sudo ./engine stop beta

### Check kernel logs
dmesg | tail

### Unload module
sudo rmmod monitor



## 3. Engineering Analysis

### Process Isolation
Linux uses namespaces (PID, mount) to isolate containers.

### Memory Monitoring
Kernel module reads RSS using mm_struct.

### IPC Mechanism
Unix sockets used for communication between CLI and supervisor.

### Scheduling Behavior
Concurrent workloads compete for CPU, affecting runtime.

### Logging Pipeline
Producer-consumer pattern used for safe logging.

---

## 4. Design Decisions and Tradeoffs

### Namespace Isolation
Choice: Used clone + namespaces  
Tradeoff: Complex debugging  
Reason: Lightweight containers

### Supervisor Design
Choice: Central supervisor process  
Tradeoff: Single point of failure  
Reason: Easier management

### IPC
Choice: Unix domain sockets  
Tradeoff: Slight overhead  
Reason: Reliable communication

### Kernel Monitor
Choice: Timer-based monitoring  
Tradeoff: Periodic delay  
Reason: Simplicity

### Scheduling Experiment
Choice: CPU-bound workload  
Tradeoff: Less realistic  
Reason: Clear observation

---

## 5. Scheduler Experiment Results

| Mode | Time |
|------|------|
| Single process | X sec |
| Parallel | Y sec |

Observation:
Parallel execution increases contention and execution time due to scheduler sharing CPU.
