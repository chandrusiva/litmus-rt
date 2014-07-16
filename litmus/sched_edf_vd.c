#include <litmus/preempt.h>
#include <litmus/sched_plugin.h>

static struct task_struct* edf_vd_schedule(struct task_struct * prev)
{
	//This call is mandatory
	sched_state_task_picked();
	//NULL means background best effort tasks will be scheduled by linux
	return NULL;
}

static long edf_vd_admit_task(struct task_struct* tsk)
{
	return -EINVAL;
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
	//.activate_plugin	= psnedf_activate_plugin,
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
