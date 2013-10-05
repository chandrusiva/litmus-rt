#ifndef __LITMUS_MCSPLOCK_H__
#define __LITMUS_MCSPLOCK_H__

#include <linux/cache.h>
#include <linux/atomic.h>

/* Based upon the implementation by B. Brandenburg:
   http://www.cs.unc.edu/~bbb/diss/ (Chapter 7) */

typedef struct __mcspnode
{
    volatile struct __mcspnode* next;
    volatile int                blocked;
} ____cacheline_aligned_in_smp mcspnode_t;

/* You get a cacheline! You get a cacheline!
   Everybody gets a cacheline! */
typedef struct
{
    atomic_long_t ____cacheline_aligned_in_smp tail;
    mcspnode_t    entries[NR_CPUS];
} mcsp_spinlock_t;

#define __MCSP_SPIN_LOCK_INITIALIZER(lockname) \
{ \
    .tail = ATOMIC_LONG_INIT(0L) \
}

#define __MCSP_SPIN_LOCK_UNLOCKED(name) \
    (mcsp_spinlock_t) __MCSP_SPIN_LOCK_INITIALIZER(name)

#define DEFINE_MCSPLOCK(x) mcsp_spinlock_t x = __MCSP_SPIN_LOCK_UNLOCKED(x) 

static __always_inline void mcsp_spin_lock_init(mcsp_spinlock_t* lock)
{
    *lock = __MCSP_SPIN_LOCK_UNLOCKED(lock);
}

static __always_inline void mcsp_spin_lock(mcsp_spinlock_t* lock)
{
    mcspnode_t* prev;
    mcspnode_t* self;
    
    self = &lock->entries[smp_processor_id()];
    self->next = NULL;

    do {
        prev = (mcspnode_t*)atomic_long_read(&lock->tail);
    } while((mcspnode_t*)atomic_long_cmpxchg(&lock->tail,
                            (long)prev, (long)self) != prev);

    if (prev) {
        self->blocked = 1;
        prev->next    = self;
        while (self->blocked)
            cpu_relax();
    }

    /* ensure references ordered before entering the critical section */
    smp_mb();
}

/* TODO: mcsp_spin_trylock() */

static __always_inline void mcsp_spin_unlock(mcsp_spinlock_t* lock)
{
    mcspnode_t* self;

    self = &lock->entries[smp_processor_id()];

    /* ensure all references within the critical section are ordered before
       we exit the critical section */
    smp_mb();

    if (!self->next &&
        ((mcspnode_t*)atomic_long_cmpxchg(&lock->tail, (long)self, 0L))!=self)
        while (!self->next)
            cpu_relax();
    if (self->next)
        self->next->blocked = 0;
}

static inline unsigned long __mcsp_spin_lock_irqsave(mcsp_spinlock_t* lock)
{
    unsigned long flags;

    local_irq_save(flags);
    preempt_disable();
    mcsp_spin_lock(lock);
    return flags;
}

static inline void __mcsp_spin_unlock_irqrestore(mcsp_spinlock_t* lock,
                unsigned long flags)
{
    mcsp_spin_unlock(lock);
    local_irq_restore(flags);
    preempt_enable();
}

#define mcsp_spin_lock_irqsave(lock, flags)    \
    do {    \
        typecheck(unsigned long, flags);    \
        flags = __mcsp_spin_lock_irqsave(lock);    \
    } while (0)

#define mcsp_spin_unlock_irqrestore(lock, flags)    \
    do {    \
        typecheck(unsigned long, flags);    \
        __mcsp_spin_unlock_irqrestore(lock, flags);    \
    } while (0)

#endif
