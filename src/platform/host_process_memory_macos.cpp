#include "host_process_memory.h"

#include <libproc.h>
#include <mach/mach.h>
#include <sys/proc_info.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

ProcessMemoryUsage QueryProcessMemory() {
    ProcessMemoryUsage     out;
    task_vm_info_data_t    info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (::task_info(::mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS) {
        return out;
    }
    out.private_bytes = info.phys_footprint;
    out.working_set   = info.resident_size;
    return out;
}

ThreadStackUsage MeasureThreadStacks() {
    ThreadStackUsage      out;
    struct proc_taskinfo  info {};
    const int             size = static_cast<int>(sizeof(info));
    if (::proc_pidinfo(::getpid(), PROC_PIDTASKINFO, 0, &info, size) != size) return out;
    out.count = static_cast<uint64_t>(info.pti_threadnum);
    return out;
}

}

}
