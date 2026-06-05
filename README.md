# Linux-D-state-tracing
## About The Project

This project combines **FUSE (Filesystem in Userspace)** and **eBPF (Extended Berkeley Packet Filter)** to build observability tool. The primary objective is to detect, trace, and debug processes that fall into the **D-state (uninterruptible sleep state)** by capturing it's user-space and kernel-space stack traces the exact moment the state transition occurs.

### How It Works

*   **Custom FUSE Filesystem:** I implemented a custom filesystem using the FUSE library. FUSE allows me to create a filesystem in user space by interacting with the kernel's Virtual File System (VFS). To simulate real-world I/O bottlenecks and deadlocks, I designed this filesystem to deliberately induce and enter a **D-state**.
*   **eBPF Kernel Probes:** While eBPF was originally designed for network packet filtering, it has evolved into a powerful tool for running safe, sandboxed programs directly inside the kernel space. Because these modules run natively in the kernel, they offer high-performance, low-overhead, and real-time tracing.
*   **Automated Diagnostics:** The moment a process within my custom filesystem switches into the uninterruptible sleep (D-state), my eBPF kernel module triggers. It immediately captures and outputs the complete **user-space and kernel-space stack traces**, providing deep visibility into the exact root cause of the block.

### Architecture Overview

```text
+-----------------------------------+
|            USER SPACE             |
|  [ Custom FUSE Filesystem Daemon] |
+-----------------+-----------------+
                  | (FUSE Protocol)
+-----------------v-----------------+
|           KERNEL SPACE            |
|  [ VFS ] ---> [ FUSE Driver ]     |
|                      |            |
|         (Enters D-State)          |
|                      v            |
|  [ eBPF Probe Hooks State Change ]|
|                      |            |
+----------------------v------------+
                       |
                       v
     [ Real-time Kernel & User Stacks ]
