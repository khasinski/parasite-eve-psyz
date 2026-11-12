// PS1 kernel events, critical sections, root counters and the interrupt
// controller behind libetc's callbacks.
#ifdef __PSP__
#include <pspkernel.h>
#endif
#include <psyz.h>
#include <psyz/log.h>
#include <kernel.h>
#include <libapi.h>
#include <libetc.h>
#include <string.h>
#include <stdlib.h>
#include "os.h"
#include "../internal.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
typedef volatile long AtomicWord;
typedef volatile __int64 AtomicTime;
static inline uint64_t AtomicTimeLoad(AtomicTime* p) {
    return (uint64_t)_InterlockedCompareExchange64(p, 0, 0);
}
static inline void AtomicTimeStore(AtomicTime* p, uint64_t v) {
    /* Exchange64 is not available on 32-bit MSVC; cmpxchg8b is. */
    uint64_t previous = AtomicTimeLoad(p);
    for (;;) {
        uint64_t observed = (uint64_t)_InterlockedCompareExchange64(
            p, (__int64)v, (__int64)previous);
        if (observed == previous)
            break;
        previous = observed;
    }
}
static inline long AtomicLoad(AtomicWord* p) { return _InterlockedOr(p, 0); }
static inline void AtomicStore(AtomicWord* p, long v) {
    _InterlockedExchange(p, v);
}
static inline int AtomicCas(AtomicWord* p, long expected, long desired) {
    return _InterlockedCompareExchange(p, desired, expected) == expected;
}
static inline void AtomicAdd(AtomicWord* p, long v) {
    _InterlockedExchangeAdd(p, v);
}
#else
typedef long AtomicWord;
typedef uint64_t AtomicTime;
static inline uint64_t AtomicTimeLoad(AtomicTime* p) {
#ifdef __PSP__
    /* Single-core Allegrex cannot emit 64-bit atomics. Keep the short
     * two-word access indivisible against interrupt/thread switches. */
    unsigned state = sceKernelCpuSuspendIntr();
    uint64_t value = *(volatile uint64_t*)p;
    sceKernelCpuResumeIntr(state);
    return value;
#else
    return __atomic_load_n(p, __ATOMIC_SEQ_CST);
#endif
}
static inline void AtomicTimeStore(AtomicTime* p, uint64_t v) {
#ifdef __PSP__
    unsigned state = sceKernelCpuSuspendIntr();
    *(volatile uint64_t*)p = v;
    sceKernelCpuResumeIntr(state);
#else
    __atomic_store_n(p, v, __ATOMIC_SEQ_CST);
#endif
}
static inline long AtomicLoad(AtomicWord* p) {
    return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}
static inline void AtomicStore(AtomicWord* p, long v) {
    __atomic_store_n(p, v, __ATOMIC_SEQ_CST);
}
static inline int AtomicCas(AtomicWord* p, long expected, long desired) {
    return __atomic_compare_exchange_n(
        p, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static inline void AtomicAdd(AtomicWord* p, long v) {
    __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}
#endif

#define EVENT_COUNT 16 // retail kernel default, see SetConf
#define EVENT_SLOT_MASK 0xFFFF

#define SYSTEM_CLOCK_HZ 33868800
#define DOT_CLOCK_HZ 6711647 // GPU clock / 8, the 320 pixel wide modes
#define HBLANK_NTSC_HZ 15732 // GPU clock / 3413 (NTSC) or / 3406 (PAL)
#define HBLANK_PAL_HZ 15620
#define MIN_PERIOD_US 32      // faster timers would only spin the host
#define MAX_CATCHUP_US 200000 // ticks owed past a longer host stall are lost

enum {
    IRQ_VBLANK,
    IRQ_GPU,
    IRQ_CDROM,
    IRQ_DMA,
    IRQ_RCNT0,
    IRQ_RCNT1,
    IRQ_RCNT2,
    IRQ_SIO0,
    IRQ_SIO,
    IRQ_SPU,
    IRQ_PIO,
    IRQ_COUNT,
};
#define IRQ_ALL ((1u << IRQ_COUNT) - 1)

typedef struct {
    AtomicWord status; // EvStUNUSED, EvStWAIT, EvStACTIVE or EvStALREADY
    unsigned int desc;
    unsigned int spec;
    long mode;
    long (*handler)();
} Event;

typedef struct {
    uint64_t last_us;   // when the count last restarted from 0
    uint64_t next_us;   // when the count reaches the target
    uint32_t period_us; // 0 until SetRCnt
    uint32_t period_frac;
    uint32_t frac_acc;
    uint32_t clock_hz;
    uint32_t ticks_per_us_q15;
    uint32_t target;
    int irq;
} RootCounter;

typedef struct {
    unsigned int desc;
    unsigned int spec;
} PendingEvent;

static Event events[EVENT_COUNT];
static AtomicWord event_waiters;

static void* kernel_lock;
static volatile uintptr_t lock_owner;
static int lock_depth;
static int in_critical_section;
static int in_interrupt;

static void* bell_events;
static void* bell_irq;
static int irq_thread_state; // 0: not started, 1: running, -1: polled

static unsigned irq_mask = 1u << IRQ_VBLANK;
static unsigned irq_latched;
static unsigned irq_saved_mask;
static int callbacks_running = 1;
static long rcnt_auto_ack[4] = {1, 1, 1, 1};
static RootCounter counters[3];
static AtomicTime poll_deadline_us;

static PendingEvent pending[32];
static int pending_len;

static void* post_lock;
static void (*posted[16])(void);
static int posted_len;

static void (*vsync_callbacks[8])(void);
static void VSyncDispatch(void);
static void (*irq_callbacks[IRQ_COUNT])(void) = {VSyncDispatch};

/* Offline playback owns emulated time. Host scheduling must not change AKAO
 * timer counts or positions within the mixed PCM stream. Fixed at startup. */
static AtomicWord audio_clock = -1;
static AtomicTime audio_time_us;
static unsigned audio_time_fraction;
static AtomicWord host_clock_paused;
static AtomicWord host_clock_speed = 1;
/* Kernel time = wall time * speed - offset (modulo 2^64). */
static AtomicTime host_clock_frozen_us, host_clock_offset_us;

static int UsesAudioClock(void) {
    long mode = AtomicLoad(&audio_clock);
    if (mode < 0) {
        const char* clock = getenv("PSYZ_AUDIO_CLOCK");
        AtomicCas(&audio_clock, -1, clock && !strcmp(clock, "video"));
        mode = AtomicLoad(&audio_clock);
    }
    return (int)mode;
}

static uint64_t KernelNowUs(void) {
    if (UsesAudioClock())
        return AtomicTimeLoad(&audio_time_us);
    if (AtomicLoad(&host_clock_paused))
        return AtomicTimeLoad(&host_clock_frozen_us);
    return Psyz_OsNowUs() * (uint64_t)AtomicLoad(&host_clock_speed) -
           AtomicTimeLoad(&host_clock_offset_us);
}

/* Emulated time for devices that run on their own clock (the CD drive): the
 * offline sample clock, or the host clock without pauses and at game speed,
 * like the root counters. */
uint64_t Psyz_KernelNowUs(void) { return KernelNowUs(); }

int Psyz_KernelOfflineClock(void) { return UsesAudioClock(); }

/* PS1 time of interrupt handlers (system clocks) not yet taken from the
 * main thread (Psyz_KernelInterruptCycles). */
static AtomicWord interrupt_debt;

static unsigned TakeInterruptDebt(void) {
    long debt = AtomicLoad(&interrupt_debt);
    while (debt && !AtomicCas(&interrupt_debt, debt, 0)) {
        debt = AtomicLoad(&interrupt_debt);
    }
    return (unsigned)debt;
}

void Psyz_KernelInterruptCycles(unsigned cycles) {
    if (!UsesAudioClock()) {
        return;
    }
    AtomicAdd(&interrupt_debt, (long)cycles);
}

unsigned Psyz_KernelTakeInterruptCycles(void) { return TakeInterruptDebt(); }

static int OwnsKernel(void);
static void SpendNs(unsigned ns);

void Psyz_SpendTime(unsigned ns) {
    // a span measured on the PS1 includes the interrupts within it
    unsigned before = TakeInterruptDebt();
    SpendNs(ns);
    TakeInterruptDebt();
    if (before) {
        Psyz_KernelInterruptCycles(before);
    }
}

static uint64_t spend_carry_ns;
static uint64_t spend_deadline_ns;
static void SpendNs(unsigned ns) {
    if (UsesAudioClock()) {
        spend_carry_ns += ns;
        if (spend_carry_ns >= 1000) {
            uint64_t us = spend_carry_ns / 1000;
            spend_carry_ns %= 1000;
            Psyz_AudioWaitUs(us);
        }
        return;
    }
    // A deadline already passed (other work ran since) restarts from now.
    uint64_t now_ns = KernelNowUs() * 1000;
    if (spend_deadline_ns < now_ns || spend_deadline_ns > now_ns + 1000000000) {
        spend_deadline_ns = now_ns;
    }
    spend_deadline_ns += ns;
    while (KernelNowUs() * 1000 < spend_deadline_ns) {
        Psyz_KernelPoll();
    }
}

/* CPU work: the interrupts that come meanwhile (and those owed by native
 * work before it) make it last longer. Inside a handler it lengthens the
 * handler. Offline clock only: on a real-time clock the native work takes
 * host time of its own and a busy wait would only lower the frame rate. */
void Psyz_SpendCycles(unsigned cycles) {
    static uint64_t carry; // clocks * 1e9 not yet spent, below one ns
    if (!UsesAudioClock()) {
        return; // the host's own time stands for it on a real-time clock
    }
    if (OwnsKernel()) {
        Psyz_KernelInterruptCycles(cycles);
        return;
    }
    do {
        uint64_t scaled = (uint64_t)cycles * 1000000000u + carry;
        carry = scaled % SYSTEM_CLOCK_HZ;
        SpendNs((unsigned)(scaled / SYSTEM_CLOCK_HZ));
    } while ((cycles = TakeInterruptDebt()) != 0);
}

static void KernelInit(void) {
    if (kernel_lock) {
        return;
    }
    post_lock = Psyz_OsLockCreate();
    bell_events = Psyz_OsBellCreate();
    bell_irq = Psyz_OsBellCreate();
    kernel_lock = Psyz_OsLockCreate();
    if (!kernel_lock || !post_lock || !bell_events || !bell_irq) {
        ERRORF("cannot create the kernel synchronisation objects");
    }
}

static int OwnsKernel(void) { return lock_owner == Psyz_OsThreadSelf(); }

static void KernelLock(void) {
    uintptr_t self = Psyz_OsThreadSelf();
    KernelInit();
    if (lock_owner == self) {
        lock_depth++;
        return;
    }
    Psyz_OsLock(kernel_lock);
    lock_owner = self;
    lock_depth = 1;
}

static void KernelUnlock(void) {
    if (--lock_depth > 0) {
        return;
    }
    lock_owner = 0;
    Psyz_OsUnlock(kernel_lock);
}

static Event* EventAt(long event) {
    unsigned long slot = (unsigned long)event & EVENT_SLOT_MASK;
    return slot < EVENT_COUNT ? &events[slot] : NULL;
}

static void DeliverLocked(unsigned int desc, unsigned int spec) {
    int became_pending = 0;
    for (int i = 0; i < EVENT_COUNT; i++) {
        Event* e = &events[i];
        if (AtomicLoad(&e->status) != EvStACTIVE || e->desc != desc ||
            e->spec != spec) {
            continue;
        }
        if (e->mode == EvMdNOINTR) {
            became_pending |= AtomicCas(&e->status, EvStACTIVE, EvStALREADY);
        } else if (e->mode == EvMdINTR && e->handler) {
            e->handler();
        }
    }
    if (became_pending && AtomicLoad(&event_waiters)) {
        Psyz_OsBellRing(bell_events);
    }
}

/* The BIOS exception handler and libetc's dispatcher around a callback
 * (DuckStation: 2.9k clocks for an RCnt interrupt with an empty callback). */
enum { KERNEL_IRQ_CYCLES = 2850 };

static void ServiceLine(int line) {
    irq_latched &= ~(1u << line);
    Psyz_KernelInterruptCycles(KERNEL_IRQ_CYCLES);
    switch (line) {
    case IRQ_VBLANK:
        DeliverLocked(RCntCNT3, EvSpINT);
        DeliverLocked(HwVBLANK, EvSpTRAP);
        break;
    case IRQ_RCNT0:
    case IRQ_RCNT1:
    case IRQ_RCNT2:
        DeliverLocked(RCntCNT0 + (line - IRQ_RCNT0), EvSpINT);
        break;
    }
    if (irq_callbacks[line]) {
        irq_callbacks[line]();
    }
}

static void DrainPosted(void) {
    for (;;) {
        void (*fn)(void) = NULL;
        Psyz_OsLock(post_lock);
        if (posted_len > 0) {
            fn = posted[0];
            posted_len--;
            memmove(posted, posted + 1, posted_len * sizeof(*posted));
        }
        Psyz_OsUnlock(post_lock);
        if (!fn) {
            return;
        }
        fn();
    }
}

// Runs every unmasked latched line, queued event and posted callback.
// Needs the kernel lock outside of a critical section.
static void ServicePending(void) {
    if (in_interrupt || in_critical_section) {
        return;
    }
    in_interrupt = 1;
    for (;;) {
        unsigned ready = irq_latched & irq_mask;
        if (ready) {
            int line = 0;
            while (!(ready & (1u << line))) {
                line++;
            }
            ServiceLine(line);
        } else if (pending_len > 0) {
            PendingEvent ev = pending[0];
            pending_len--;
            memmove(pending, pending + 1, pending_len * sizeof(*pending));
            DeliverLocked(ev.desc, ev.spec);
        } else if (posted_len > 0) {
            DrainPosted();
        } else {
            break;
        }
    }
    in_interrupt = 0;
}

static void LatchLine(int line) {
    KernelLock();
    irq_latched |= 1u << line;
    ServicePending();
    KernelUnlock();
}

void Psyz_KernelRaise(unsigned int desc, unsigned int spec) {
    KernelLock();
    if (pending_len < LEN(pending)) {
        pending[pending_len].desc = desc;
        pending[pending_len].spec = spec;
        pending_len++;
    } else {
        WARNF("dropped event %08X/%04X", desc, spec);
    }
    ServicePending();
    KernelUnlock();
}

static void StartIrqThread(void);
void Psyz_KernelPost(void (*fn)(void)) {
    KernelInit();
    Psyz_OsLock(post_lock);
    if (posted_len < LEN(posted)) {
        posted[posted_len++] = fn;
    } else {
        WARNF("dropped a posted interrupt");
    }
    Psyz_OsUnlock(post_lock);
    StartIrqThread();
    Psyz_OsBellRing(bell_irq);
}

void Psyz_KernelVBlank(void) {
    Psyz_KernelPoll();
    LatchLine(IRQ_VBLANK);
}

/* Library code that the PS1 ran atomically with respect to an interrupt
 * handler (libsnd's sound API against its sequencer tick, which only checks
 * _snd_ev_flag) holds the kernel lock: the interrupt callbacks, which run
 * under it on the timer thread, wait instead of running concurrently. A PS1
 * interrupt could not run in parallel, nor see the state half updated. */
void Psyz_KernelLockInterrupts(void) { KernelLock(); }

void Psyz_KernelUnlockInterrupts(void) { KernelUnlock(); }

void PS1_EnterCriticalSection(void) {
    if (OwnsKernel()) {
        return; // already in one, or inside an interrupt handler
    }
    KernelLock();
    in_critical_section = 1;
}

void PS1_ExitCriticalSection(void) {
    if (!OwnsKernel() || !in_critical_section) {
        return;
    }
    in_critical_section = 0;
    ServicePending();
    KernelUnlock();
}

long OpenEvent(unsigned long desc, long spec, long mode, long (*func)()) {
    long id = -1;
    KernelLock();
    for (int i = 0; i < EVENT_COUNT; i++) {
        Event* e = &events[i];
        if (AtomicLoad(&e->status) == EvStUNUSED) {
            e->desc = (unsigned int)desc;
            e->spec = (unsigned int)spec;
            e->mode = mode;
            e->handler = func;
            AtomicStore(&e->status, EvStWAIT);
            id = (long)(DescEV | (unsigned)i);
            break;
        }
    }
    KernelUnlock();
    return id;
}

long CloseEvent(long event) {
    Event* e = EventAt(event);
    if (!e) {
        return 0;
    }
    KernelLock();
    AtomicStore(&e->status, EvStUNUSED);
    KernelUnlock();
    return 1;
}

static long SetEnabled(long event, long status) {
    Event* e = EventAt(event);
    if (!e) {
        return 0;
    }
    KernelLock();
    if (AtomicLoad(&e->status) != EvStUNUSED) {
        AtomicStore(&e->status, status);
    }
    KernelUnlock();
    return 1;
}

long EnableEvent(long event) { return SetEnabled(event, EvStACTIVE); }

long DisableEvent(long event) { return SetEnabled(event, EvStWAIT); }

long TestEvent(long event) {
    Event* e = EventAt(event);
    if (!e) {
        return 0;
    }
    if (AtomicCas(&e->status, EvStALREADY, EvStACTIVE)) {
        return 1;
    }
    if (irq_thread_state < 0) {
        Psyz_KernelPoll();
    }
    return 0;
}

long WaitEvent(long event) {
    Event* e = EventAt(event);
    long ret;
    int resume_critical_section;
    if (!e) {
        return 0;
    }
    if (AtomicCas(&e->status, EvStALREADY, EvStACTIVE)) {
        return 1;
    }
    if (AtomicLoad(&e->status) != EvStACTIVE) {
        return 0;
    }
    // interrupts cannot be delivered to a thread sleeping in a critical section
    resume_critical_section = OwnsKernel() && in_critical_section;
    if (resume_critical_section) {
        PS1_ExitCriticalSection();
    }
    KernelInit();
    AtomicAdd(&event_waiters, 1);
    for (;;) {
        if (AtomicCas(&e->status, EvStALREADY, EvStACTIVE)) {
            ret = 1;
            break;
        }
        if (AtomicLoad(&e->status) != EvStACTIVE) {
            ret = 0;
            break;
        }
        if (irq_thread_state < 0) {
            Psyz_KernelPoll();
            Psyz_OsBellWait(bell_events, 1000);
        } else {
            Psyz_OsBellWait(bell_events, 100000);
        }
    }
    AtomicAdd(&event_waiters, -1);
    if (resume_critical_section) {
        PS1_EnterCriticalSection();
    }
    return ret;
}

void DeliverEvent(unsigned ev1, unsigned ev2) {
    KernelLock();
    DeliverLocked(ev1, ev2);
    KernelUnlock();
}

void UnDeliverEvent(unsigned ev1, unsigned ev2) {
    KernelLock();
    for (int i = 0; i < EVENT_COUNT; i++) {
        Event* e = &events[i];
        if (e->desc == ev1 && e->spec == ev2 && e->mode == EvMdNOINTR) {
            AtomicCas(&e->status, EvStALREADY, EvStACTIVE);
        }
    }
    KernelUnlock();
}

static int CounterLine(long spec) {
    unsigned i = (unsigned)spec & 0xFFFF;
    return i < 3 ? IRQ_RCNT0 + (int)i : i == 3 ? IRQ_VBLANK : -1;
}

static unsigned CounterClock(int i, long mode) {
    if (mode & RCntMdSC) {
        return SYSTEM_CLOCK_HZ;
    }
    switch (i) {
    case 0:
        return DOT_CLOCK_HZ;
    case 1:
        return GetVideoMode() == MODE_PAL ? HBLANK_PAL_HZ : HBLANK_NTSC_HZ;
    default:
        return SYSTEM_CLOCK_HZ / 8;
    }
}

static int IsDue(uint64_t deadline_us, uint64_t now_us) {
    return now_us >= deadline_us;
}

static void CounterStep(RootCounter* c) {
    c->last_us = c->next_us;
    c->next_us += c->period_us;
    c->frac_acc += c->period_frac;
    if (c->frac_acc >= c->clock_hz) {
        c->frac_acc -= c->clock_hz;
        c->next_us++;
    }
}

static void CounterRestart(RootCounter* c, uint64_t now_us) {
    c->last_us = now_us;
    c->next_us = now_us + c->period_us;
    c->frac_acc = 0;
}

/* One-shot device completions on the emulated clock (an MDEC transfer),
 * fired as interrupts once their time comes, like the root counters. */
#define TIMED_MAX 8
static struct {
    uint64_t at_us;
    void (*fn)(void);
} timed[TIMED_MAX];
static int timed_len;

static void StartIrqThread(void);
void Psyz_KernelPostAt(uint64_t at_us, void (*fn)(void)) {
    StartIrqThread();
    KernelLock();
    if (timed_len < TIMED_MAX) {
        timed[timed_len].at_us = at_us;
        timed[timed_len].fn = fn;
        timed_len++;
        uint64_t deadline = AtomicTimeLoad(&poll_deadline_us);
        if (at_us < deadline) {
            AtomicTimeStore(&poll_deadline_us, at_us);
        }
    } else {
        WARNF("dropped a timed interrupt");
    }
    KernelUnlock();
    Psyz_OsBellRing(bell_irq);
}

void Psyz_KernelCancel(void (*fn)(void)) {
    KernelLock();
    for (int i = 0; i < timed_len;) {
        if (timed[i].fn == fn) {
            timed[i] = timed[--timed_len];
        } else {
            i++;
        }
    }
    KernelUnlock();
}

// Fires the due one-shot completions, earliest first; returns 1 with the
// next one's time in *next_us when some remain.
static int ServiceTimed(uint64_t now, uint64_t* next_us) {
    for (;;) {
        int first = -1;
        for (int i = 0; i < timed_len; i++) {
            if (first < 0 || timed[i].at_us < timed[first].at_us) {
                first = i;
            }
        }
        if (first < 0) {
            return 0;
        }
        if (!IsDue(timed[first].at_us, now) || in_critical_section) {
            *next_us = timed[first].at_us;
            return 1;
        }
        void (*fn)(void) = timed[first].fn;
        timed[first] = timed[--timed_len];
        fn();
    }
}

// Fires every timer interrupt that came due. Returns 0 when no timer is
// triggered, otherwise 1 with the earliest deadline in *deadline_us.
static int ServiceTimers(uint64_t* deadline_us) {
    int triggered = 0;
    KernelLock();
    uint64_t now = KernelNowUs();
    in_interrupt = 1;
    for (int i = 0; i < LEN(counters); i++) {
        RootCounter* c = &counters[i];
        unsigned bit = 1u << (IRQ_RCNT0 + i);
        if (!c->period_us || !c->irq) {
            continue;
        }
        if (now > c->next_us && now - c->next_us > MAX_CATCHUP_US) {
            CounterRestart(c, now);
        }
        while (IsDue(c->next_us, now)) {
            CounterStep(c);
            if (irq_mask & bit) {
                ServiceLine(IRQ_RCNT0 + i);
            } else {
                irq_latched |= bit;
            }
        }
        if (!triggered || c->next_us < *deadline_us) {
            *deadline_us = c->next_us;
            triggered = 1;
        }
    }
    uint64_t timed_next;
    if (ServiceTimed(now, &timed_next) &&
        (!triggered || timed_next < *deadline_us)) {
        *deadline_us = timed_next;
        triggered = 1;
    }
    in_interrupt = 0;
    AtomicTimeStore(
        &poll_deadline_us, triggered ? *deadline_us : now + INT32_MAX);
    ServicePending();
    KernelUnlock();
    return triggered;
}

static void IrqThread(void) {
    for (;;) {
        uint64_t deadline;
        if (!ServiceTimers(&deadline)) {
            Psyz_OsBellWait(bell_irq, PSYZ_OS_FOREVER);
            continue;
        }
        uint64_t now = KernelNowUs();
        if (deadline > now) {
            uint64_t left = deadline - now;
            Psyz_OsBellWait(
                bell_irq, (uint32_t)(left < INT32_MAX ? left : INT32_MAX));
        }
    }
}

static void StartIrqThread(void) {
    if (irq_thread_state) {
        return;
    }
    KernelInit();
    if (UsesAudioClock()) {
        irq_thread_state = -1;
        return;
    }
    irq_thread_state = Psyz_OsThreadStart(IrqThread, "psyz_irq") ? -1 : 1;
}

/* Desktop modal/focus pauses exclude wall time from root counters too.
 * Freezing only video/audio leaves AKAO sequencing ahead on return. */
void Psyz_KernelPauseHostClock(int paused) {
    if (UsesAudioClock())
        return;
    KernelLock();
    if (paused && !AtomicLoad(&host_clock_paused)) {
        AtomicTimeStore(&host_clock_frozen_us, KernelNowUs());
        AtomicStore(&host_clock_paused, 1);
    } else if (!paused && AtomicLoad(&host_clock_paused)) {
        AtomicTimeStore(
            &host_clock_offset_us,
            Psyz_OsNowUs() * (uint64_t)AtomicLoad(&host_clock_speed) -
                AtomicTimeLoad(&host_clock_frozen_us));
        AtomicStore(&host_clock_paused, 0);
    }
    KernelUnlock();
    Psyz_OsBellRing(bell_irq);
}

/* Root counters run `speed` times faster from now on, without a jump.
 * Offline playback takes its time from the mixed audio instead. */
void Psyz_KernelSetHostClockSpeed(unsigned speed) {
    if (UsesAudioClock())
        return;
    KernelLock();
    if (AtomicLoad(&host_clock_paused)) {
        AtomicStore(&host_clock_speed, (long)speed);
    } else {
        uint64_t now = KernelNowUs();
        AtomicStore(&host_clock_speed, (long)speed);
        AtomicTimeStore(
            &host_clock_offset_us, Psyz_OsNowUs() * (uint64_t)speed - now);
    }
    KernelUnlock();
    Psyz_OsBellRing(bell_irq);
}

void Psyz_KernelPoll(void) {
    uint64_t deadline;
    if (!irq_thread_state || OwnsKernel() ||
        (!UsesAudioClock() &&
         !IsDue(AtomicTimeLoad(&poll_deadline_us), KernelNowUs()))) {
        return;
    }
    ServiceTimers(&deadline);
}

/* Keep a timer edge between sample batches, rather than moving every AKAO
 * update to the video-frame boundary. These endpoints are private to SDL. */
unsigned Psyz_KernelAudioBatch(unsigned maximum) {
    if (!UsesAudioClock())
        return maximum;
    Psyz_KernelPoll();
    if (!irq_thread_state)
        return maximum;
    uint64_t deadline = AtomicTimeLoad(&poll_deadline_us);
    uint64_t now = AtomicTimeLoad(&audio_time_us);
    if (deadline <= now)
        return 1;
    uint64_t until =
        (deadline - now) * PSYZ_SPU_SAMPLE_RATE - audio_time_fraction;
    uint64_t frames = (until + 999999) / 1000000;
    return frames < maximum ? (unsigned)(frames ? frames : 1) : maximum;
}

void Psyz_KernelAdvanceAudio(unsigned frames) {
    if (!UsesAudioClock())
        return;
    uint64_t fraction = audio_time_fraction + (uint64_t)frames * 1000000;
    AtomicTimeStore(&audio_time_us, AtomicTimeLoad(&audio_time_us) +
                                        fraction / PSYZ_SPU_SAMPLE_RATE);
    audio_time_fraction = (unsigned)(fraction % PSYZ_SPU_SAMPLE_RATE);
    Psyz_KernelPoll();
}

long SetRCnt(long spec, unsigned short target, long mode) {
    int i = (int)(spec & 0xFFFF);
    if (i >= 3) {
        return 0;
    }
    StartIrqThread();
    KernelLock();
    RootCounter* c = &counters[i];
    uint32_t clock = CounterClock(i, mode);
    // target * 1000000 / clock split in two steps to stay within 32 bits
    uint32_t scaled = (target ? target : 0x10000) * 15625u;
    uint32_t rem = scaled % clock * 64;
    c->clock_hz = clock;
    c->target = target ? target : 0x10000;
    c->period_us = scaled / clock * 64 + rem / clock;
    c->period_frac = rem % clock;
    if (c->period_us < MIN_PERIOD_US) {
        c->period_us = MIN_PERIOD_US;
        c->period_frac = 0;
    }
    c->ticks_per_us_q15 = clock / 15625 * 512 + clock % 15625 * 512 / 15625;
    c->irq = (mode & RCntMdINTR) != 0;
    CounterRestart(c, KernelNowUs());
    AtomicTimeStore(&poll_deadline_us, c->last_us);
    KernelUnlock();
    Psyz_OsBellRing(bell_irq);
    return 1;
}

long GetRCnt(long spec) {
    int i = (int)(spec & 0xFFFF);
    if (i >= 3) {
        return 0;
    }
    KernelLock();
    RootCounter* c = &counters[i];
    long value = 0;
    if (c->period_us) {
        uint32_t phase = (KernelNowUs() - c->last_us) % c->period_us;
        uint32_t ticks = phase * c->ticks_per_us_q15 >> 15;
        value = (long)(ticks < c->target ? ticks : c->target - 1);
    }
    KernelUnlock();
    return value;
}

long ResetRCnt(long spec) {
    int i = (int)(spec & 0xFFFF);
    if (i >= 3) {
        return 0;
    }
    KernelLock();
    CounterRestart(&counters[i], KernelNowUs());
    AtomicTimeStore(&poll_deadline_us, counters[i].last_us);
    KernelUnlock();
    Psyz_OsBellRing(bell_irq);
    return 1;
}

long StartRCnt(long spec) {
    int line = CounterLine(spec);
    if (line < 0) {
        return 0;
    }
    KernelLock();
    irq_mask |= 1u << line;
    ServicePending();
    KernelUnlock();
    Psyz_OsBellRing(bell_irq);
    return line != IRQ_VBLANK;
}

long StopRCnt(long spec) {
    int line = CounterLine(spec);
    if (line < 0) {
        return 0;
    }
    KernelLock();
    irq_mask &= ~(1u << line);
    KernelUnlock();
    return 1;
}

long ChangeClearRCnt(long t, long flag) {
    long prev;
    if (t < 0 || t >= LEN(rcnt_auto_ack)) {
        return 0;
    }
    prev = rcnt_auto_ack[t];
    rcnt_auto_ack[t] = flag;
    return prev;
}

static void VSyncDispatch(void) {
    for (int i = 0; i < LEN(vsync_callbacks); i++) {
        if (vsync_callbacks[i]) {
            vsync_callbacks[i]();
        }
    }
}

int VSyncCallbacks(int ch, void (*f)()) {
    void (*prev)(void);
    if (ch < 0 || ch >= LEN(vsync_callbacks)) {
        return 0;
    }
    KernelLock();
    prev = vsync_callbacks[ch];
    vsync_callbacks[ch] = f;
    KernelUnlock();
    return (int)(intptr_t)prev;
}

int VSyncCallback(void (*f)()) { return VSyncCallbacks(4, f); }

void* InterruptCallback(int irq, void (*cb)()) {
    void (*prev)(void);
    if (irq < 0 || irq >= IRQ_COUNT) {
        return NULL;
    }
    if (cb && irq >= IRQ_RCNT0 && irq <= IRQ_RCNT2) {
        StartIrqThread();
    }
    KernelLock();
    prev = irq_callbacks[irq];
    irq_callbacks[irq] = cb;
    if (cb) {
        irq_mask |= 1u << irq;
    } else {
        irq_mask &= ~(1u << irq);
    }
    ServicePending();
    KernelUnlock();
    Psyz_OsBellRing(bell_irq);
    return (void*)prev;
}

int SetIntrMask(int mask) {
    int prev;
    KernelLock();
    prev = (int)irq_mask;
    irq_mask = (unsigned)mask & IRQ_ALL;
    ServicePending();
    KernelUnlock();
    return prev;
}

int GetIntrMask(void) { return (int)irq_mask; }

int ResetCallback(void) {
    KernelLock();
    if (callbacks_running) {
        KernelUnlock();
        return 0;
    }
    memset(irq_callbacks, 0, sizeof(irq_callbacks));
    memset(vsync_callbacks, 0, sizeof(vsync_callbacks));
    irq_callbacks[IRQ_VBLANK] = VSyncDispatch;
    irq_mask = 1u << IRQ_VBLANK;
    irq_latched = 0;
    callbacks_running = 1;
    KernelUnlock();
    return 1;
}

int StopCallback(void) {
    KernelLock();
    if (!callbacks_running) {
        KernelUnlock();
        return 0;
    }
    irq_saved_mask = irq_mask;
    irq_mask = 0;
    irq_latched = 0;
    callbacks_running = 0;
    KernelUnlock();
    return 1;
}

int RestartCallback(void) {
    KernelLock();
    if (callbacks_running) {
        KernelUnlock();
        return 0;
    }
    irq_mask = irq_saved_mask;
    callbacks_running = 1;
    ServicePending();
    KernelUnlock();
    return 1;
}

int CheckCallback(void) { return OwnsKernel() && in_interrupt; }

#if PE_NATIVE_STATE_CPU_COMPONENTS
static uintptr_t state_kernel_owner;
int Psyz_StateKernelBegin(void){if(OwnsKernel())return 0;KernelLock();if(in_interrupt||in_critical_section||state_kernel_owner){KernelUnlock();return 0;}state_kernel_owner=Psyz_OsThreadSelf();return 1;}
void Psyz_StateKernelEnd(void){if(state_kernel_owner==Psyz_OsThreadSelf()&&OwnsKernel()){state_kernel_owner=0;KernelUnlock();}}
#endif

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "psyz/state_producers.h"
#include "psyz/state_audio.h"
static long (*state_event_handlers[64])();static unsigned state_event_count;
static void (*state_callbacks[64])(void);static unsigned state_callback_count;
static uint32_t state_handler_id(long (*p)()){if(!p)return 0;for(unsigned i=0;i<state_event_count;i++)if(state_event_handlers[i]==p)return i+1;if(state_event_count==64)return UINT32_MAX;state_event_handlers[state_event_count++]=p;return state_event_count;}
static uint32_t state_callback_id(void (*p)(void)){if(!p)return 0;for(unsigned i=0;i<state_callback_count;i++)if(state_callbacks[i]==p)return i+1;if(state_callback_count==64)return UINT32_MAX;state_callbacks[state_callback_count++]=p;return state_callback_count;}
static void state_kernel_put(unsigned char *o,uint32_t v){for(unsigned i=0;i<4;i++)o[i]=(unsigned char)(v>>(i*8));}
static uint32_t state_kernel_get(const unsigned char *i){return(uint32_t)i[0]|(uint32_t)i[1]<<8|(uint32_t)i[2]<<16|(uint32_t)i[3]<<24;}
int Psyz_StateKernelOwned(void){return state_kernel_owner==Psyz_OsThreadSelf()&&OwnsKernel();}
#define STATE_KERNEL_BYTES 912u
size_t Psyz_StateKernelBytes(void){return STATE_KERNEL_BYTES;}
static int state_kernel_walk(const unsigned char *in,unsigned char *out,int apply){size_t at=8;uint64_t now=KernelNowUs();uint32_t v;
#define VALUE(value,check) do{v=out?(uint32_t)(value):state_kernel_get(in+at);if(!(check))return 0;if(out)state_kernel_put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
#define HANDLER(value) do{v=out?state_handler_id(value):state_kernel_get(in+at);if(v==UINT32_MAX||v>state_event_count)return 0;if(out)state_kernel_put(out+at,v);else if(apply)(value)=v?state_event_handlers[v-1]:0;at+=4;}while(0)
#define CALLBACK(value) do{v=out?state_callback_id(value):state_kernel_get(in+at);if(v==UINT32_MAX||v>state_callback_count)return 0;if(out)state_kernel_put(out+at,v);else if(apply)(value)=v?state_callbacks[v-1]:0;at+=4;}while(0)
VALUE(audio_time_fraction,v<PSYZ_SPU_SAMPLE_RATE);long state_clock=AtomicLoad(&audio_clock),state_speed=AtomicLoad(&host_clock_speed),state_paused=AtomicLoad(&host_clock_paused);VALUE(state_clock,v==(uint32_t)AtomicLoad(&audio_clock));VALUE(state_speed,v==(uint32_t)AtomicLoad(&host_clock_speed));VALUE(state_paused,v==(uint32_t)AtomicLoad(&host_clock_paused));
for(unsigned i=0;i<16;i++){long status=AtomicLoad(&events[i].status);VALUE(status,v==EvStUNUSED||v==EvStWAIT||v==EvStACTIVE||v==EvStALREADY);if(apply&&!out)AtomicStore(&events[i].status,status);VALUE(events[i].desc,1);VALUE(events[i].spec,1);VALUE(events[i].mode,v==0||v==EvMdINTR||v==EvMdNOINTR);HANDLER(events[i].handler);}
VALUE(irq_mask,!(v&~IRQ_ALL));VALUE(irq_latched,!(v&~IRQ_ALL));VALUE(irq_saved_mask,!(v&~IRQ_ALL));VALUE(callbacks_running,v<=1);for(unsigned i=0;i<4;i++){VALUE(rcnt_auto_ack[i],v<=1);}
for(unsigned i=0;i<3;i++){RootCounter *c=&counters[i];uint64_t age=c->period_us?now-c->last_us:0,left=c->period_us?c->next_us-now:0;uint32_t lo=(uint32_t)age,hi=(uint32_t)(age>>32),l=(uint32_t)left,h=(uint32_t)(left>>32);VALUE(lo,1);VALUE(hi,1);VALUE(l,1);VALUE(h,1);VALUE(c->period_us,v<=864000000u);VALUE(c->period_frac,1);VALUE(c->frac_acc,1);VALUE(c->clock_hz,1);VALUE(c->ticks_per_us_q15,1);VALUE(c->target,v<=65536);VALUE(c->irq,v<=1);if(!out){uint32_t clock=state_kernel_get(in+at-16),fraction=state_kernel_get(in+at-24),acc=state_kernel_get(in+at-20);if(clock?(fraction>=clock||acc>=clock):(fraction||acc))return 0;uint64_t a=(uint64_t)lo|((uint64_t)hi<<32);int64_t left=(int64_t)((uint64_t)l|((uint64_t)h<<32));if(a>86400000000ull||left< -86400000000ll||left>86400000000ll)return 0;}if(apply&&!out){c->last_us=c->period_us?now-((uint64_t)lo|((uint64_t)hi<<32)):0;c->next_us=c->period_us?now+((uint64_t)l|((uint64_t)h<<32)):0;}}
VALUE(pending_len,v<=32);for(unsigned i=0;i<32;i++){VALUE(pending[i].desc,1);VALUE(pending[i].spec,1);}
VALUE(posted_len,v<=16);for(unsigned i=0;i<16;i++){CALLBACK(posted[i]);}
for(unsigned i=0;i<8;i++){CALLBACK(vsync_callbacks[i]);}for(unsigned i=0;i<IRQ_COUNT;i++){CALLBACK(irq_callbacks[i]);}
#undef VALUE
#undef HANDLER
#undef CALLBACK
if(apply&&!out){uint64_t deadline=now+MAX_CATCHUP_US;for(unsigned i=0;i<3;i++)if(counters[i].period_us&&IsDue(counters[i].next_us,deadline))deadline=counters[i].next_us;AtomicTimeStore(&poll_deadline_us,deadline);Psyz_OsBellRing(bell_irq);if(pending_len||posted_len)Psyz_OsBellRing(bell_events);}
return at==STATE_KERNEL_BYTES;}
int Psyz_StateKernelCapture(unsigned char *out,size_t bytes){if(!out||bytes!=STATE_KERNEL_BYTES||!Psyz_StateKernelOwned()||!Psyz_StateAudioOwned()||AtomicLoad(&event_waiters))return 0;Psyz_OsLock(post_lock);memcpy(out,"PEKERN01",8);int ok=state_kernel_walk(0,out,0);Psyz_OsUnlock(post_lock);return ok;}
int Psyz_StateKernelValidate(const unsigned char *in,size_t bytes){return in&&bytes==STATE_KERNEL_BYTES&&!memcmp(in,"PEKERN01",8)&&Psyz_StateKernelOwned()&&Psyz_StateAudioOwned()&&!AtomicLoad(&event_waiters)&&state_kernel_walk(in,0,0);}
int Psyz_StateKernelRestore(const unsigned char *in,size_t bytes){if(!Psyz_StateKernelValidate(in,bytes))return 0;Psyz_OsLock(post_lock);int ok=state_kernel_walk(in,0,1);Psyz_OsUnlock(post_lock);return ok;}
#endif
