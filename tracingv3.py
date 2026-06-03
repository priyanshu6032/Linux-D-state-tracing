from bcc import BPF

bpf_text = """
#include <linux/sched.h>

#define TASK_UNINTERRUPTIBLE 2

struct data_t {
    u32 pid;
    int user_stack_id;
    int kern_stack_id;
    char comm[TASK_COMM_LEN];
};

BPF_HASH(target_pids, u32, u8);   // track newfs PIDs
BPF_STACK_TRACE(stack_traces, 20480);
BPF_PERF_OUTPUT(events);

TRACEPOINT_PROBE(sched, sched_switch) { //k-probes attached to sched_switch

    u32 next_pid = args->next_pid;
    u32 prev_pid = args->prev_pid;

    char comm[TASK_COMM_LEN];

    bpf_probe_read_kernel_str(&comm, sizeof(comm), args->next_comm);

    char target[] = "newfs";

    if (__builtin_memcmp(comm, target, sizeof(target)) == 0) { //filter out all newfs process
        u8 one = 1;
        target_pids.update(&next_pid, &one);
    }

    u8 *exists = target_pids.lookup(&prev_pid);

    if (exists && args->prev_state == TASK_UNINTERRUPTIBLE) { //the filtered process are again checked if it is in D-state

        struct data_t data = {};
        data.pid = prev_pid;

        bpf_probe_read_kernel_str(&data.comm, sizeof(data.comm), args->prev_comm);

        // Capture stacks
        data.user_stack_id = stack_traces.get_stackid(args, BPF_F_USER_STACK);
        data.kern_stack_id = stack_traces.get_stackid(args, 0);

        events.perf_submit(args, &data, sizeof(data));
    }

    return 0;
}
"""
b = BPF(text=bpf_text)

def print_stack(stack_id, pid, is_kernel=False):
    if stack_id < 0:
        print("    [Stack Error or Empty]")
        return

    stack_traces = b["stack_traces"]
    for addr in stack_traces.walk(stack_id):
        if is_kernel:
            sym = b.ksym(addr)
        else:
            sym = b.sym(addr, pid, show_module=True)

        print("    %s" % sym.decode('utf-8', 'replace'))


def print_event(cpu, data, size):
    event = b["events"].event(data)

    print(f"\n{'='*60}")
    print(f"PID: {event.pid} | COMM: {event.comm.decode(errors='replace')} | ENTERED D-STATE")

    print("\n  KERNEL STACK:")
    print_stack(event.kern_stack_id, event.pid, True)

    print("\n  USER STACK:")
    print_stack(event.user_stack_id, event.pid, False)


b["events"].open_perf_buffer(print_event)

while True:
    try:
        b.perf_buffer_poll()
    except KeyboardInterrupt:
        exit()