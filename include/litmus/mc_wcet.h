#ifndef _MC_WCET_H_
#define _MC_WCET_H_

/*To include linked list implementation */
#include <litmus/list_userspace.h>

/*litmus time type */
typedef unsigned long long lt_t;

/*Declaration of structure which will hold multiple wcet values */

struct exec_times{
	lt_t wcet_val;
	lt_t vd;
	struct list_head_u list;
};

#endif

