
/*This is the plugin implementation for EDF-VD scheduler */

#include <litmus/preempt.h>
#include <litmus/sched_plugin.h>
#include <linux/percpu.h>
#include <litmus/litmus.h>
#include <litmus/edf_common.h>
//edf_common includes rt_domain header, so uncomment below..
//#include <litmus/rt_domain.h>
#include <litmus/jobs.h>
#include <litmus/budget.h>

//This struct is not typedefd in tutorial
typedef struct {
	rt_domain_t 		local_queues;
	int          		cpu;
	struct task_struct* 	scheduled; /* only RT tasks */
} edf_vd_domain_t;

//Not defined as static in built-in plugins
static DEFINE_PER_CPU(edf_vd_domain_t, edf_vd_domains);

//Used with different names in built-in plugins
#define cpu_state_for(cpu_id)   (&per_cpu(edf_vd_domains, cpu_id))
#define local_cpu_state()       (&__get_cpu_var(edf_vd_domains))

/* this helper is called when task `prev` exhausted its budget or when
 * it signaled a job completion */
static void edf_vd_job_completion(struct task_struct *prev, int budget_exhausted)
{
        /* call common helper code to compute the next release time, deadline,
         * etc. */
        prepare_for_next_period(prev);
}

/* Add the task `tsk` to the appropriate queue. Assumes caller holds the ready lock.
 */
//rt_domain is passed directly in builtin, cpu entry is passed here.
static void edf_vd_requeue(struct task_struct *tsk, edf_vd_domain_t *cpu_state)
{
        if (is_released(tsk, litmus_clock())) {
                /* Uses __add_ready() instead of add_ready() because we already
                 * hold the ready lock. */
                __add_ready(&cpu_state->local_queues, tsk);
        } else {
                /* Uses add_release() because we DON'T have the release lock. */
                add_release(&cpu_state->local_queues, tsk);
        }
}
//job_completed is used here. It denotes sleep which is used in builtin plugins
static struct task_struct* edf_vd_schedule(struct task_struct * prev)
{
	edf_vd_domain_t  *local_state = local_cpu_state();

        /* next == NULL means "schedule background work". */
        struct task_struct *next = NULL;

        /* prev's task state */
        int exists, out_of_time, job_completed, self_suspends, preempt, resched;

        raw_spin_lock(&local_state->local_queues.ready_lock);

        BUG_ON(local_state->scheduled && local_state->scheduled != prev);
        BUG_ON(local_state->scheduled && !is_realtime(prev));

        exists = local_state->scheduled != NULL;
        self_suspends = exists && !is_running(prev);
        out_of_time   = exists && budget_enforced(prev)
                && budget_exhausted(prev);
        job_completed = exists && is_completed(prev);

        /* preempt is true if task `prev` has lower priority than something on
         * the ready queue. */
        preempt = edf_preemption_needed(&local_state->local_queues, prev);

        /* check all conditions that make us reschedule */
        resched = preempt;

        /* if `prev` suspends, it CANNOT be scheduled anymore => reschedule */
        if (self_suspends)
                resched = 1;

        /* also check for (in-)voluntary job completions */
        if (out_of_time || job_completed) {
                demo_job_completion(prev, out_of_time);
                resched = 1;
        }

        if (resched) {
                /* First check if the previous task goes back onto the ready
                 * queue, which it does if it did not self_suspend.
                 */
                if (exists && !self_suspends)
                        edf_vd_requeue(prev, local_state);
                next = __take_ready(&local_state->local_queues);
        } else
                /* No preemption is required. */
                next = local_state->scheduled;

        local_state->scheduled = next;


        if (exists && prev != next)
                TRACE_TASK(prev, "descheduled.\n");
        if (next)
                TRACE_TASK(next, "scheduled.\n");

        /* This mandatory. It triggers a transition in the LITMUS^RT remote
         * preemption state machine. Call this AFTER the plugin has made a local
         * scheduling decision.
         */
        sched_state_task_picked();

        raw_spin_unlock(&local_state->local_queues.ready_lock);

        return next;
}

static long edf_vd_admit_task(struct task_struct* tsk)
{
	TRACE_TASK(tsk, "Rejected by EDF-VD plugin..\n");
	return -EINVAL;
}

static long edf_vd_activate_plugin(void)
    {
            //The following is done in domain_init in built-in plugins
	    int cpu;
            edf_vd_domain_t *state;
    
            for_each_online_cpu(cpu) {
                   TRACE("Initializing CPU%d...\n", cpu);
    
                   state = cpu_state_for(cpu);
   
                   state->cpu = cpu;
                   state->scheduled = NULL;
                   edf_domain_init(&state->local_queues, NULL, NULL);
            }
   
           return 0;
    }

/*	Plugin object	*/
//cacheline aligned attribute is given in builtin plugins
static struct sched_plugin edf_vd_plugin __cacheline_aligned_in_smp = {
	.plugin_name		= "EDF-VD",
	//.task_new		= psnedf_task_new,
	//.complete_job		= complete_job,
	//.task_exit		= psnedf_task_exit,
	.schedule		= edf_vd_schedule,
	//.task_wake_up		= psnedf_task_wake_up,
	//.task_block		= psnedf_task_block,
	.admit_task		= edf_vd_admit_task,
	.activate_plugin	= edf_vd_activate_plugin,
	//.deactivate_plugin	= psnedf_deactivate_plugin,
	//.get_domain_proc_info	= psnedf_get_domain_proc_info,
};


static int __init init_edf_vd(void)
{
	/*
	int i;
	*/
	/* We do not really want to support cpu hotplug, do we? ;)
	 * However, if we are so crazy to do so,
	 * we cannot use num_online_cpu()
	 */
	/*
	for (i = 0; i < num_online_cpus(); i++) {
		psnedf_domain_init(remote_pedf(i),
				   psnedf_check_resched,
				   NULL, i);
	}
	*/
	return register_sched_plugin(&edf_vd_plugin);
}

module_init(init_edf_vd);
