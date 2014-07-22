
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
#include <litmus/litmus_proc.h>

//Include header containing global extern variables
#include <litmus/mc_global.h>


//This struct is not typedefd in tutorial
typedef struct {
	rt_domain_t 		local_queues;
	int          		cpu;
	struct task_struct* 	scheduled; /* only RT tasks */
} edf_vd_domain_t;

//Not defined as static in built-in plugins
static DEFINE_PER_CPU(edf_vd_domain_t, edf_vd_domains);

static struct domain_proc_info edf_vd_domain_proc_info;

//Used with different names in built-in plugins
#define cpu_state_for(cpu_id)   (&per_cpu(edf_vd_domains, cpu_id))
#define local_cpu_state()       (&__get_cpu_var(edf_vd_domains))

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

static void edf_vd_task_new(struct task_struct *tsk, int on_runqueue,
                          int is_running)
{
        unsigned long flags; /* needed to store the IRQ flags */
        edf_vd_domain_t *state = cpu_state_for(0);
        lt_t now;

        TRACE_TASK(tsk, "is a new RT task %llu (on_rq:%d, running:%d)\n",
                   litmus_clock(), on_runqueue, is_running);

        /* acquire the lock protecting the state and disable interrupts */
        raw_spin_lock_irqsave(&state->local_queues.ready_lock, flags);

        now = litmus_clock();

        //update the wcet and vd of this task
	update_wcet_vd(tsk);
	
	/* the first job exists starting as of right now */
        release_at(tsk, now);

        if (is_running) {
                /* if tsk is running, then no other task can be running
                 * on the local CPU */
                BUG_ON(state->scheduled != NULL);
                state->scheduled = tsk;
        } else if (on_runqueue) {
                edf_vd_requeue(tsk, state);
        }
	
	if (edf_preemption_needed(&state->local_queues, state->scheduled))
                           preempt_if_preemptable(state->scheduled, state->cpu);
        
	raw_spin_unlock_irqrestore(&state->local_queues.ready_lock, flags);
}

static void edf_vd_task_exit(struct task_struct *tsk)
{
        unsigned long flags; /* needed to store the IRQ flags */
        edf_vd_domain_t *state = cpu_state_for(0);

        /* acquire the lock protecting the state and disable interrupts */
        raw_spin_lock_irqsave(&state->local_queues.ready_lock, flags);

        if (state->scheduled == tsk)
                state->scheduled = NULL;

        /* For simplicity, we assume here that the task is no longer queued anywhere else. This
         * is the case when tasks exit by themselves; additional queue management is
         * is required if tasks are forced out of real-time mode by other tasks. */

        raw_spin_unlock_irqrestore(&state->local_queues.ready_lock, flags);
}

static int edf_vd_check_for_preemption_on_release(rt_domain_t *local_queues)
{
        edf_vd_domain_t *state = container_of(local_queues, edf_vd_domain_t,
                                                    local_queues);

        /* Because this is a callback from rt_domain_t we already hold
         * the necessary lock for the ready queue.
         */

        if (edf_preemption_needed(local_queues, state->scheduled)) {
                preempt_if_preemptable(state->scheduled, state->cpu);
                return 1;
        } else
                return 0;
}

/* this helper is called when task `prev` exhausted its budget or when
 * it signaled a job completion */
static void edf_vd_job_completion(struct task_struct *prev, int budget_exhausted)
{	
	//update the wcet and vd for next job
	update_wcet_vd(prev);

	/* call common helper code to compute the next release time, deadline,
         * etc. */	
        prepare_for_next_period(prev);
}


//job_completed is used here. It denotes sleep which is used in builtin plugins
static struct task_struct* edf_vd_schedule(struct task_struct * prev)
{
	edf_vd_domain_t  *local_state = local_cpu_state();

        /* next == NULL means "schedule background work". */
        struct task_struct *next = NULL;

        /* prev's task state */
        int exists, job_completed, self_suspends, preempt, resched;
	
	int out_of_time;
        
	raw_spin_lock(&local_state->local_queues.ready_lock);

        BUG_ON(local_state->scheduled && local_state->scheduled != prev);
        BUG_ON(local_state->scheduled && !is_realtime(prev));

        exists = local_state->scheduled != NULL;
        self_suspends = exists && !is_running(prev);
        out_of_time   = exists && budget_exhausted(prev);
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
        //There is no budget enforcement option in mc systems, its mandatory..
	if (job_completed || out_of_time) {
                edf_vd_job_completion(prev,0);
                resched = 1;
        }

        if (resched) {
                /* First check if the previous task goes back onto the ready
                 * queue, which it does if it did not self_suspend.
                 */
                //Requeue after checking whether the task is at its allowable crit level..
		if (exists && !self_suspends)
		{
                        if(prev->rt_param.task_params.task_cl <= sys_cl)
				edf_vd_requeue(prev, local_state);
		}
		
		do{
                	next = __take_ready(&local_state->local_queues);
			if(next==NULL)
			{
				//Re-initialize everything
				TRACE("ready queue is empty. syscl reinitialized to its normal value\n");
				sys_cl = temp_sys_cl;
				budget_flag=0;	
				break;
			}
		}while(next->rt_param.task_params.task_cl>sys_cl);



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

/* Called when the state of tsk changes back to TASK_RUNNING.
 * We need to requeue the task.
 *
 * NOTE: if a sporadic task suspended for a long time,
 * this might actually be an event-driven release of a new job.
 *
 */
static void edf_vd_task_resume(struct task_struct  *tsk)
{
        unsigned long flags; /* needed to store the IRQ flags */
        edf_vd_domain_t *state = cpu_state_for(0);
        lt_t now;

        TRACE_TASK(tsk, "wake_up at %llu\n", litmus_clock());

        /* acquire the lock protecting the state and disable interrupts */
        raw_spin_lock_irqsave(&state->local_queues.ready_lock, flags);

        now = litmus_clock();
	
	//update the wcet and vd for the resumed job
	update_wcet_vd(tsk);
	
	//allow this to the ready to release queue only if task_cl is legal.
        if(tsk->rt_param.task_params.task_cl <= sys_cl)
	 {
        	if (is_sporadic(tsk) && is_tardy(tsk, now)) {
                /* This sporadic task was gone for a "long" time and woke up past
                 * its deadline. Give it a new budget by triggering a job
                 * release. */
                	release_at(tsk, now);
        	}

       		 /* This check is required to avoid races with tasks that resume before
	         * the scheduler "noticed" that it resumed. That is, the wake up may
	         * race with the call to schedule(). */
	        if (state->scheduled != tsk)
		{
        	        edf_vd_requeue(tsk, state);
			if (edf_preemption_needed(&state->local_queues, state->scheduled))
                        	   preempt_if_preemptable(state->scheduled, state->cpu);	
		}
	}
        raw_spin_unlock_irqrestore(&state->local_queues.ready_lock, flags);
}

static long edf_vd_admit_task(struct task_struct* tsk)
{
	if (task_cpu(tsk) == 0) {
                TRACE_TASK(tsk, "accepted by edf vd plugin.\n");
                return 0;
        } else
                return -EINVAL;
}

static long edf_vd_get_domain_proc_info(struct domain_proc_info **ret)
{
        *ret = &edf_vd_domain_proc_info;
        return 0;
}

static void edf_vd_setup_domain_proc(void)
{
   
        int num_rt_cpus = 1;

        struct cd_mapping *cpu_map, *domain_map;

        memset(&edf_vd_domain_proc_info, sizeof(edf_vd_domain_proc_info), 0);
        init_domain_proc_info(&edf_vd_domain_proc_info, num_rt_cpus, num_rt_cpus);
        edf_vd_domain_proc_info.num_cpus = num_rt_cpus;
        edf_vd_domain_proc_info.num_domains = num_rt_cpus;

                cpu_map = &edf_vd_domain_proc_info.cpu_to_domains[0];
                domain_map = &edf_vd_domain_proc_info.domain_to_cpus[0];

                cpu_map->id = 0;
                domain_map->id = 0;
                cpumask_set_cpu(0, cpu_map->mask);
                cpumask_set_cpu(0, domain_map->mask);
                
        
}

static long edf_vd_activate_plugin(void)
    {
            //The following is done in domain_init in built-in plugins
            edf_vd_domain_t *state;
    
                   TRACE("Initializing CPU%d...\n", 0);
    
                   state = cpu_state_for(0);
   
                   state->cpu = 0;
                   state->scheduled = NULL;
                   edf_domain_init(&state->local_queues, edf_vd_check_for_preemption_on_release, NULL);
            
   		
	    edf_vd_setup_domain_proc();
            return 0;
    }

static long edf_vd_deactivate_plugin(void)
{
        destroy_domain_proc_info(&edf_vd_domain_proc_info);
        return 0;
}

/*	Plugin object	*/
//cacheline aligned attribute is given in builtin plugins
//wake_up is being called as resume here..
static struct sched_plugin edf_vd_plugin __cacheline_aligned_in_smp = {
	.plugin_name		= "EDF-VD",
	.task_new		= edf_vd_task_new,
	//.complete_job		= complete_job,
	.task_exit		= edf_vd_task_exit,
	.schedule		= edf_vd_schedule,
	.task_wake_up		= edf_vd_task_resume,
	//.task_block		= psnedf_task_block,
	.admit_task		= edf_vd_admit_task,
	.activate_plugin	= edf_vd_activate_plugin,
	.deactivate_plugin	= edf_vd_deactivate_plugin,
	.get_domain_proc_info	= edf_vd_get_domain_proc_info,
};


static int __init init_edf_vd(void)
{
	return register_sched_plugin(&edf_vd_plugin);
}

module_init(init_edf_vd);
