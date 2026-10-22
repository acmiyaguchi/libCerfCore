#include "host_crash_capture.h"

#include "host_app_bundle.h"

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/ucontext.h>

#if !(defined(__arm64__) || defined(__x86_64__))
#error "host_crash_capture_macos.cpp has no thread register capture for this host"
#endif

namespace cerfcore {

namespace host_platform {

namespace {

bool ThreadRegisters(thread_act_t thread, uintptr_t& pc, uintptr_t& sp, uintptr_t& fp) {
#if defined(__arm64__)
    arm_thread_state64_t   state{};
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    if (::thread_get_state(thread, ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&state),
                           &count) != KERN_SUCCESS) {
        return false;
    }
    pc = __darwin_arm_thread_state64_get_pc(state);
    sp = __darwin_arm_thread_state64_get_sp(state);
    fp = __darwin_arm_thread_state64_get_fp(state);
#else
    x86_thread_state64_t   state{};
    mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
    if (::thread_get_state(thread, x86_THREAD_STATE64, reinterpret_cast<thread_state_t>(&state),
                           &count) != KERN_SUCCESS) {
        return false;
    }
    pc = static_cast<uintptr_t>(state.__rip);
    sp = static_cast<uintptr_t>(state.__rsp);
    fp = static_cast<uintptr_t>(state.__rbp);
#endif
    return true;
}

long ThreadIdOf(thread_act_t thread) {
    thread_identifier_info_data_t info{};
    mach_msg_type_number_t        count = THREAD_IDENTIFIER_INFO_COUNT;
    if (::thread_info(thread, THREAD_IDENTIFIER_INFO, reinterpret_cast<thread_info_t>(&info), &count) !=
        KERN_SUCCESS) {
        return 0;
    }
    return static_cast<long>(info.thread_id);
}

}

long CrashThreadId() {
    uint64_t id = 0;
    ::pthread_threadid_np(nullptr, &id);
    return static_cast<long>(id);
}

void CrashContextRegisters(const void* uc, uintptr_t& pc, uintptr_t& sp, uintptr_t& fp) {
    const auto& state = static_cast<const ucontext_t*>(uc)->uc_mcontext->__ss;
#if defined(__arm64__)
    pc = __darwin_arm_thread_state64_get_pc(state);
    sp = __darwin_arm_thread_state64_get_sp(state);
    fp = __darwin_arm_thread_state64_get_fp(state);
#else
    pc = static_cast<uintptr_t>(state.__rip);
    sp = static_cast<uintptr_t>(state.__rsp);
    fp = static_cast<uintptr_t>(state.__rbp);
#endif
}

void CrashLogDirectory(char* out, size_t cap) {
    char        raw[PATH_MAX];
    char        real[PATH_MAX];
    uint32_t    size = sizeof(raw);
    const char* path = nullptr;
    if (::_NSGetExecutablePath(raw, &size) == 0) {
        path = ::realpath(raw, real) != nullptr ? real : raw;
    }
    size_t length = 0;
    if (path != nullptr) {
        for (; path[length] != '\0' && length + 1 < cap; ++length) out[length] = path[length];
    }
    const size_t bundle = AppBundleLength(out, length);
    const size_t end    = bundle != 0 ? bundle : length;
    size_t       cut    = 0;
    for (size_t i = 0; i < end; ++i) {
        if (out[i] == '/') cut = i + 1;
    }
    out[cut] = '\0';
}

int CrashCaptureOtherThreads(CrashThreadState* out, int capacity) {
    thread_act_array_t     threads = nullptr;
    mach_msg_type_number_t count   = 0;
    if (::task_threads(::mach_task_self(), &threads, &count) != KERN_SUCCESS) return 0;
    const thread_act_t self     = ::mach_thread_self();
    int                captured = 0;
    for (mach_msg_type_number_t i = 0; i < count; ++i) {
        if (threads[i] != self && ::thread_suspend(threads[i]) == KERN_SUCCESS) {
            uintptr_t pc = 0, sp = 0, fp = 0;
            if (captured < capacity && ThreadRegisters(threads[i], pc, sp, fp)) {
                out[captured++] = CrashThreadState{ThreadIdOf(threads[i]), pc, sp, fp};
            }
        }
        ::mach_port_deallocate(::mach_task_self(), threads[i]);
    }
    ::mach_port_deallocate(::mach_task_self(), self);
    ::vm_deallocate(::mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                    count * sizeof(thread_act_t));
    return captured;
}

}

}
