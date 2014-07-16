#include <litmus/preempt.h>
#include <litmus/sched_plugin.h>
#include <linux/percpu.h>
#include <litmus/litmus.h>
#include <litmus/edf_common.h>
//edf_common includes rt_domain header, so uncomment below..
//#include <litmus/rt_domain.h>

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


static struct task_struct* edf_vd_schedule(struct task_struct * prev)
{
	//This call is mandatory
	sched_state_task_picked();
	//NULL means background best effort tasks will be scheduled by linux
	return NULL;
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
