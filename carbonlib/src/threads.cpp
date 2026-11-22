/*+===================================================================
  File:      threads.cpp

  Summary:   Implements the Carbon Thread Manager's cooperative PowerPC scheduler.

  Classes:   Declared: ROMlib_ppc_threads, Thread, ThreadHook. Used: ROMlib_ppc_thread_environment,
             std::bad_alloc, std::unique_ptr, std::vector.

  Functions: ROMlib_ppc_threads::ROMlib_ppc_threads, ROMlib_ppc_threads::allocate_thread,
             ROMlib_ppc_threads::create, ROMlib_ppc_threads::create_pool, ROMlib_ppc_threads::dispose,
             ROMlib_ppc_threads::find, ROMlib_ppc_threads::get_task_ref, ROMlib_ppc_threads::host_switch,
             ROMlib_ppc_threads::invoke, ROMlib_ppc_threads::memory, ROMlib_ppc_threads::notify,
             ROMlib_ppc_threads::notify_dispose, ROMlib_ppc_threads::publish_ready,
             ROMlib_ppc_threads::put, ROMlib_ppc_threads::ready, ROMlib_ppc_threads::reap, ROMlib_ppc_threads::service,
             ROMlib_ppc_threads::schedule_hook, ROMlib_ppc_threads::select_thread,
             ROMlib_ppc_threads::set_scheduler, ROMlib_ppc_threads::start, ROMlib_ppc_threads::switch_to,
             ROMlib_ppc_threads::~ROMlib_ppc_threads,
             ROMlib_ppc_threads_destroy, ROMlib_ppc_threads_handles, ROMlib_ppc_threads_service, Thread::Thread,
             Thread::~Thread

  Origin:    Original scheduler following Apple's Thread Manager Reference; no Apple executable code.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "carbon_threads.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

namespace {
constexpr uint32_t default_stack_size = 65536;
constexpr uint32_t maximum_stack_size = 16 * 1024 * 1024;
constexpr size_t thread_limit = 128;
constexpr uint32_t new_suspend = 1, use_premade = 2, create_if_needed = 4, exact_match = 16;
}

struct ROMlib_ppc_threads;

struct ThreadHook {
    uint32_t procedure = 0, parameter = 0;
};

struct Thread {
    ROMlib_ppc_threads* owner;
    uint32_t id, state = 0, entry = 0, argument = 0, address = 0, size = 0, requested_size = 0, result_address = 0,
                 result = 0, stack_pointer = 0;
    void *fiber = nullptr, *stack = nullptr;
    Thread* return_to = nullptr;
    ThreadHook switch_in, switch_out, terminator;
    bool started = false, completed = false, cancelled = false, recycle = false, ready_requested = false;

    Thread(ROMlib_ppc_threads* manager, uint32_t number) : owner(manager), id(number)
    {
    }

    ~Thread();
};

struct ROMlib_ppc_threads {
    ROMlib_ppc_thread_environment env;
    Thread main, *current;
    std::vector<std::unique_ptr<Thread>> children;
    std::vector<std::unique_ptr<Thread>> pool;
    uint32_t next_id = 3, critical = 0;
    uint32_t task_ref = 0;
    void* task_token = nullptr;
    uint32_t scheduler = 0, debugger_new = 0, debugger_dispose = 0, debugger_scheduler = 0;
    uint32_t scheduler_info_address = 0;
    void* scheduler_info = nullptr;
    bool converted = false;

    explicit ROMlib_ppc_threads(const ROMlib_ppc_thread_environment& e) : env(e), main(this, 2), current(&main)
    {
        main.state = 2;
    }

    void* memory(uint32_t address, size_t size)
    {
        return address ? env.memory(env.opaque, address, size, 1) : nullptr;
    }

    bool put(uint32_t address, uint32_t value, unsigned width = 4)
    {
        auto* bytes = static_cast<unsigned char*>(memory(address, width));
        if (!bytes)
            return false;
        for (unsigned i = 0; i < width; ++i)
            bytes[i] = value >> (8 * (width - i - 1));
        return true;
    }

    Thread* find(uint32_t id)
    {
        if (id == 1)
            return current;
        if (id == 2)
            return &main;
        for (auto& item : children)
            if (item->id == id && !item->completed)
                return item.get();
        return nullptr;
    }

    int get_task_ref(uint32_t output)
    {
        if (!memory(output, 4))
            return -50;
        if (!task_token) {
            task_token = env.allocate(env.opaque, 4, 4, &task_ref);
            if (!task_token)
                return -108;
            std::memset(task_token, 0, 4);
        }
        return put(output, task_ref) ? 0 : -50;
    }

    void publish_ready()
    {
        const auto update = [](Thread& thread) {
            if (thread.ready_requested && !thread.cancelled && thread.state == 1)
                thread.state = 0;
            thread.ready_requested = false;
        };
        update(main);
        for (auto& thread : children)
            update(*thread);
    }

    Thread* ready()
    {
        if (!critical)
            publish_ready();
        Thread* first = main.state == 0 ? &main : nullptr;
        for (auto& item : children) {
            if (!item->completed && item->state == 0) {
                if (item->id > current->id)
                    return item.get();
                if (!first)
                    first = item.get();
            }
        }
        return first;
    }

    bool invoke(uint32_t procedure, uint32_t* args, unsigned count, uint32_t* result = nullptr)
    {
        return procedure && !env.exiting(env.opaque) && env.invoke(env.opaque, procedure, args, count, result);
    }

    void notify(const Thread& thread, ThreadHook hook)
    {
        uint32_t args[] = {thread.id, hook.parameter};
        invoke(hook.procedure, args, 2);
    }

    void notify_dispose(Thread& thread)
    {
        notify(thread, thread.terminator);
        uint32_t id = thread.id;
        invoke(debugger_dispose, &id, 1);
    }

    Thread* schedule_hook(uint32_t procedure, Thread* suggested)
    {
        if (!procedure || env.exiting(env.opaque))
            return suggested;
        put(scheduler_info_address, 16);
        put(scheduler_info_address + 4, current->completed ? 0 : current->id);
        put(scheduler_info_address + 8, suggested ? suggested->id : 0);
        put(scheduler_info_address + 12, 0); // Cooperative fibers have no interrupted preemptive thread.
        uint32_t args[] = {scheduler_info_address};
        uint32_t choice;
        if (!invoke(procedure, args, 1, &choice))
            return suggested;
        Thread* selected = choice ? find(choice) : nullptr;
        return selected && selected->state != 1 ? selected : ready();
    }

    Thread* select_thread(Thread* suggested)
    {
        return schedule_hook(debugger_scheduler, schedule_hook(scheduler, suggested));
    }

    int set_scheduler(uint32_t& slot, uint32_t procedure)
    {
        if (procedure && !scheduler_info) {
            scheduler_info = env.allocate(env.opaque, 16, 4, &scheduler_info_address);
            if (!scheduler_info)
                return -108;
        }
        slot = procedure;
        return 0;
    }

    void reap()
    {
        for (auto it = children.begin(); it != children.end();) {
            auto& thread = *it;
            if (!thread->completed || thread.get() == current) {
                ++it;
                continue;
            }
            if (thread->recycle) {
                DeleteFiber(thread->fiber);
                thread->fiber = nullptr;
                pool.push_back(std::move(thread));
            }
            it = children.erase(it);
        }
    }

    std::unique_ptr<Thread> allocate_thread(uint32_t size)
    {
        auto thread = std::make_unique<Thread>(this, 0);
        thread->requested_size = size;
        thread->size = (size + 15) & ~15u;
        thread->stack = env.allocate(env.opaque, thread->size, 16, &thread->address);
        if (!thread->stack)
            return nullptr;
        return thread;
    }

    int create_pool(uint32_t style, int16_t count, uint32_t size)
    {
        size = size ? size : default_stack_size;
        if (style != 1 || count < 0 || size < 4096 || size > maximum_stack_size)
            return -50;
        if (static_cast<size_t>(count) > thread_limit - children.size() - pool.size())
            return -617;
        if (!count)
            return 0;
        pool.reserve(thread_limit);
        std::vector<std::unique_ptr<Thread>> pending;
        pending.reserve(count);
        for (int16_t i = 0; i < count; ++i) {
            auto thread = allocate_thread(size);
            if (!thread)
                return -108;
            pending.push_back(std::move(thread));
        }
        for (auto& thread : pending)
            pool.push_back(std::move(thread));
        return 0;
    }

    static void host_switch(void* fiber)
    {
        SwitchToFiber(fiber);
    }

    void switch_to(Thread* target)
    {
        Thread* from = current;
        if (from == target)
            return;
        if (!from->completed && !from->cancelled)
            notify(*from, from->switch_out);
        if (from->state == 2)
            from->state = 0;
        target->state = 2;
        current = target;
        env.switch_context(env.opaque, host_switch, target->fiber);
        if (!from->cancelled)
            notify(*from, from->switch_in);
    }

    static VOID WINAPI start(void* argument)
    {
        auto* thread = static_cast<Thread*>(argument);
        ROMlib_ppc_threads& scheduler = *thread->owner;
        thread->started = true;
        scheduler.notify(*thread, thread->switch_in);
        uint32_t result = 0;
        const int success = !scheduler.env.exiting(scheduler.env.opaque) &&
                            scheduler.env.execute(scheduler.env.opaque, thread->entry, thread->argument,
                                                  thread->address + thread->size - 256, &result);
        if (!thread->cancelled)
            thread->result = result;
        if (thread->result_address)
            scheduler.put(thread->result_address, thread->result);
        if (!thread->cancelled)
            scheduler.notify_dispose(*thread);
        thread->completed = true;
        thread->state = 1;
        Thread* target = thread->return_to;
        if (!target && success && !scheduler.env.exiting(scheduler.env.opaque))
            target = scheduler.select_thread(scheduler.ready());
        if (!target)
            target = &scheduler.main;
        scheduler.switch_to(target);
        // A completed fiber is deleted by its resumed scheduler, never resumed.
        std::abort();
    }

    int create(ROMlib_ppc_registers* r)
    {
        if (!put(r->gpr[9], 0))
            return -50;
        const uint32_t options = r->gpr[7], size = r->gpr[6] ? r->gpr[6] : default_stack_size;
        if (r->gpr[3] != 1 || (options & ~31u) || size < 4096 || size > maximum_stack_size ||
            (r->gpr[8] && !memory(r->gpr[8], 4)))
            return -50;
        auto pooled = pool.end();
        if (options & use_premade) {
            for (auto it = pool.begin(); it != pool.end(); ++it) {
                const uint32_t available = (*it)->requested_size;
                if (available >= size && (!(options & exact_match) || available == size) &&
                    (pooled == pool.end() || available < (*pooled)->requested_size))
                    pooled = it;
            }
            if (pooled == pool.end() && !(options & create_if_needed))
                return -108;
        }
        if ((pooled == pool.end() && children.size() + pool.size() >= thread_limit) || next_id == UINT32_MAX)
            return -617;
        uint32_t entry = env.resolve(env.opaque, r->gpr[4]);
        if (!entry)
            return -50;
        if (!main.fiber) {
            if (IsThreadAFiber())
                main.fiber = GetCurrentFiber();
            else {
                main.fiber = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
                converted = main.fiber != nullptr;
            }
            if (!main.fiber)
                return -108;
        }
        children.reserve(children.size() + 1);
        auto fresh = pooled == pool.end() ? allocate_thread(size) : nullptr;
        Thread* thread = pooled == pool.end() ? fresh.get() : pooled->get();
        if (!thread)
            return -108;
        thread->fiber = CreateFiberEx(65536, 1024 * 1024, FIBER_FLAG_FLOAT_SWITCH, start, thread);
        if (!thread->fiber)
            return -108;
        thread->id = next_id++;
        thread->entry = entry;
        thread->argument = r->gpr[5];
        thread->result_address = r->gpr[8];
        thread->state = options & new_suspend ? 1 : 0;
        thread->stack_pointer = thread->address + thread->size - 256;
        thread->result = 0;
        thread->return_to = nullptr;
        thread->switch_in = thread->switch_out = thread->terminator = {};
        thread->started = thread->completed = thread->cancelled = thread->recycle = false;
        thread->ready_requested = false;
        std::memset(thread->stack, 0, thread->size);
        const uint32_t id = thread->id;
        if (pooled == pool.end())
            children.push_back(std::move(fresh));
        else {
            children.push_back(std::move(*pooled));
            pool.erase(pooled);
        }
        put(r->gpr[9], id);
        uint32_t created = id;
        invoke(debugger_new, &created, 1);
        return 0;
    }

    int dispose(Thread* thread, uint32_t result, bool recycle, ROMlib_ppc_registers* r)
    {
        if (!thread || thread == &main)
            return thread ? -619 : -618;
        if (critical)
            return -619;
        if (recycle)
            pool.reserve(thread_limit);
        thread->recycle = recycle;
        thread->result = result;
        if (thread->result_address)
            put(thread->result_address, result);
        notify_dispose(*thread);
        thread->cancelled = true;
        if (thread == current) {
            r->lr = 0; // The bridge replaces this with the invocation's halt vector.
            return 1;
        }
        if (thread->started) {
            thread->return_to = current;
            switch_to(thread);
        }
        else {
            thread->completed = true;
        }
        reap();
        return 0;
    }

    int service(const char* name, ROMlib_ppc_registers* r, bool save_stack)
    {
        reap();
        if (save_stack)
            current->stack_pointer = r->gpr[1];
        int error = 0;
        if (!std::strcmp(name, "NewThread"))
            error = create(r);
        else if (!std::strcmp(name, "GetCurrentThread"))
            error = put(r->gpr[3], current->id) ? 0 : -50;
        else if (!std::strcmp(name, "ThreadBeginCritical")) {
            if (critical == UINT32_MAX)
                error = -619;
            else
                ++critical;
        }
        else if (!std::strcmp(name, "ThreadEndCritical")) {
            if (!critical)
                error = -619;
            else
                --critical;
        }
        else if (!std::strcmp(name, "GetThreadCurrentTaskRef"))
            error = get_task_ref(r->gpr[3]);
        else if (!std::strcmp(name, "SetThreadScheduler"))
            error = set_scheduler(scheduler, r->gpr[3]);
        else if (!std::strcmp(name, "GetThreadScheduler"))
            error = put(r->gpr[3], scheduler) ? 0 : -50;
        else if (!std::strcmp(name, "SetDebuggerNotificationProcs")) {
            error = set_scheduler(debugger_scheduler, r->gpr[5]);
            if (!error) {
                debugger_new = r->gpr[3];
                debugger_dispose = r->gpr[4];
            }
        }
        else if (!std::strcmp(name, "SetThreadSwitcher") || !std::strcmp(name, "SetThreadTerminator")) {
            if (Thread* thread = find(r->gpr[3])) {
                ThreadHook& slot = !std::strcmp(name, "SetThreadTerminator") ? thread->terminator
                                   : static_cast<uint8_t>(r->gpr[6])         ? thread->switch_in
                                                                             : thread->switch_out;
                slot = {r->gpr[4], r->gpr[5]};
            }
            else
                error = -618;
        }
        else if (!std::strcmp(name, "GetThreadState") || !std::strcmp(name, "GetThreadStateGivenTaskRef") ||
                 !std::strcmp(name, "SetThreadReadyGivenTaskRef")) {
            const bool ordinary = !std::strcmp(name, "GetThreadState");
            const bool mark_ready = !std::strcmp(name, "SetThreadReadyGivenTaskRef");
            if (!ordinary && (!task_ref || r->gpr[3] != task_ref))
                error = -619;
            else if (Thread* thread = find(r->gpr[ordinary ? 3 : 4])) {
                if (mark_ready)
                    thread->ready_requested = true;
                else
                    error = put(r->gpr[ordinary ? 4 : 5], thread->state, 2) ? 0 : -50;
            }
            else
                error = -618;
        }
        else if (!std::strcmp(name, "ThreadCurrentStackSpace")) {
            if (Thread* thread = find(r->gpr[3])) {
                const uint32_t sp = thread->stack_pointer;
                const uint32_t bytes = thread == &main ? env.stack_space(env.opaque, sp)
                                       : sp >= thread->address && sp - thread->address <= thread->size
                                           ? sp - thread->address
                                           : 0;
                error = put(r->gpr[4], bytes) ? 0 : -50;
            }
            else
                error = -618;
        }
        else if (!std::strcmp(name, "GetDefaultThreadStackSize"))
            error = r->gpr[3] != 1 ? -50 : put(r->gpr[4], default_stack_size) ? 0 : -50;
        else if (!std::strcmp(name, "CreateThreadPool"))
            error = create_pool(r->gpr[3], static_cast<int16_t>(r->gpr[4]), r->gpr[5]);
        else if (!std::strcmp(name, "GetFreeThreadCount"))
            error = r->gpr[3] != 1 ? -50 : put(r->gpr[4], pool.size(), 2) ? 0 : -50;
        else if (!std::strcmp(name, "GetSpecificFreeThreadCount")) {
            const auto count = std::count_if(pool.begin(), pool.end(),
                                             [&](const auto& thread) { return thread->requested_size == r->gpr[4]; });
            error = r->gpr[3] != 1 ? -50 : put(r->gpr[5], count, 2) ? 0 : -50;
        }
        else if (!std::strcmp(name, "DisposeThread"))
            error = dispose(find(r->gpr[3]), r->gpr[4], static_cast<uint8_t>(r->gpr[5]) != 0, r);
        else if (!std::strcmp(name, "YieldToThread") || !std::strcmp(name, "YieldToAnyThread")) {
            uint32_t id = !std::strcmp(name, "YieldToThread") ? r->gpr[3] : 0;
            if (id && !critical)
                publish_ready();
            Thread* target = id ? find(id) : ready();
            if (critical)
                error = -619;
            else if (id && (!target || target->state == 1))
                error = -618;
            else if (Thread* selected = select_thread(target))
                switch_to(selected);
        }
        else if (!std::strcmp(name, "SetThreadState") || !std::strcmp(name, "SetThreadStateEndCritical")) {
            if (!std::strcmp(name, "SetThreadStateEndCritical")) {
                if (!critical)
                    error = -619;
                else
                    --critical;
            }
            Thread* thread = find(r->gpr[3]);
            uint32_t state = r->gpr[4] & 65535;
            if (!error && (!thread || state > 1))
                error = thread ? -50 : -618;
            if (!error && thread == current && state == 1) {
                if (r->gpr[5] && !critical)
                    publish_ready();
                Thread* target = r->gpr[5] ? find(r->gpr[5]) : ready();
                if (!critical && target && target->state != 1)
                    target = select_thread(target);
                if (critical || !target || target == current || target->state == 1)
                    error = -619;
                else {
                    thread->state = 1;
                    switch_to(target);
                }
            }
            else if (!error) {
                thread->ready_requested = false;
                thread->state = thread == current ? 2 : state;
            }
        }
        reap();
        if (current->cancelled)
            return 2; // End this guest invocation cleanly, unwinding its C++ scopes.
        r->gpr[3] = static_cast<uint32_t>(error);
        return error == 1 ? 2 : 1;
    }

    ~ROMlib_ppc_threads()
    {
        critical = 0;
        while (!children.empty()) {
            Thread* thread = children.back().get();
            if (!thread->completed && thread->started) {
                thread->cancelled = true;
                thread->return_to = &main;
                switch_to(thread);
            }
            children.pop_back();
        }
        if (converted)
            ConvertFiberToThread();
        if (task_token)
            env.release(env.opaque, task_token);
        if (scheduler_info)
            env.release(env.opaque, scheduler_info);
    }
};

Thread::~Thread()
{
    if (id != 2 && fiber)
        DeleteFiber(fiber);
    if (stack)
        owner->env.release(owner->env.opaque, stack);
}

extern "C" int ROMlib_ppc_threads_handles(const char* name)
{
    static const char* names[] = {"NewThread",
                                  "DisposeThread",
                                  "GetCurrentThread",
                                  "GetThreadCurrentTaskRef",
                                  "GetThreadStateGivenTaskRef",
                                  "SetThreadReadyGivenTaskRef",
                                  "ThreadCurrentStackSpace",
                                  "SetThreadScheduler",
                                  "GetThreadScheduler",
                                  "SetThreadSwitcher",
                                  "SetThreadTerminator",
                                  "SetDebuggerNotificationProcs",
                                  "GetThreadState",
                                  "SetThreadState",
                                  "SetThreadStateEndCritical",
                                  "GetDefaultThreadStackSize",
                                  "CreateThreadPool",
                                  "GetFreeThreadCount",
                                  "GetSpecificFreeThreadCount",
                                  "ThreadBeginCritical",
                                  "ThreadEndCritical",
                                  "YieldToThread",
                                  "YieldToAnyThread"};
    for (const char* item : names)
        if (!std::strcmp(name, item))
            return 1;
    return 0;
}

extern "C" int ROMlib_ppc_threads_service(ROMlib_ppc_threads** scheduler,
                                          const ROMlib_ppc_thread_environment* environment, const char* name,
                                          ROMlib_ppc_registers* registers, int save_stack)
{
    if (!ROMlib_ppc_threads_handles(name))
        return 0;
    if (!std::strcmp(name, "NewThread")) {
        void* made = registers->gpr[9] ? environment->memory(environment->opaque, registers->gpr[9], 4, 1) : nullptr;
        if (!made) {
            registers->gpr[3] = static_cast<uint32_t>(-50);
            return 1;
        }
        std::memset(made, 0, 4);
    }
    try {
        if (!*scheduler)
            *scheduler = new ROMlib_ppc_threads(*environment);
        return (*scheduler)->service(name, registers, save_stack != 0);
    }
    catch (const std::bad_alloc&) {
        registers->gpr[3] = static_cast<uint32_t>(-108);
        return 1;
    }
}

extern "C" void ROMlib_ppc_threads_destroy(ROMlib_ppc_threads* scheduler)
{
    delete scheduler;
}
