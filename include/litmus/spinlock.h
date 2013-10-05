#ifndef __LITMUS_SPINLOCK_H__
#define __LITMUS_SPINLOCK_H__

#include <linux/spinlock.h>

#ifdef CONFIG_LITMUS_SPINLOCKS

#include <litmus/mcsplock.h>

typedef mcsp_spinlock_t litmus_spinlock_t;
#define DEFINE_LITMUS_SPINLOCK(x)                  DEFINE_MCSPLOCK(x)
#define litmus_spin_lock_init(lock)                mcsp_spin_lock_init(lock)
#define litmus_spin_lock(lock)                     mcsp_spin_lock(lock)
#define litmus_spin_unlock(lock)                   mcsp_spin_unlock(lock)
#define litmus_spin_lock_irqsave(lock, flags)    \
        mcsp_spin_lock_irqsave(lock, flags)
#define litmus_spin_unlock_irqrestore(lock, flags)    \
        mcsp_spin_unlock_irqrestore(lock, flags)

#else

typedef raw_spinlock_t litmus_spinlock_t;
#define DEFINE_LITMUS_SPINLOCK(x)                  DEFINE_RAW_SPINLOCK(x)
#define litmus_spin_lock_init(lock)                raw_spin_lock_init(lock)
#define litmus_spin_lock(lock)                     raw_spin_lock(lock)
#define litmus_spin_unlock(lock)                   raw_spin_unlock(lock)
#define litmus_spin_lock_irqsave(lock, flags)    \
        raw_spin_lock_irqsave(lock, flags)
#define litmus_spin_unlock_irqrestore(lock, flags)    \
        raw_spin_unlock_irqrestore(lock, flags)

#endif
#endif
