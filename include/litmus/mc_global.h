#ifndef _MC_GLOBAL_H_
#define _MC_GLOBAL_H_

/* This is the system criticality level indicator which will be 
 * accessed across all files.
 */
extern int sys_cl;

//As condition check variable in budget enforcement
extern int budget_flag;

//This stores the initial sys_cl. This is used to return to the normal mode
extern int temp_sys_cl;

#endif
