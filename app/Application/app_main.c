#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "ai/fish_bbox_npu_test.hpp"

/* usermain関数 */
EXPORT INT usermain(void)
{
	T_CTSK fish_task = {
		.itskpri = 10,
		.stksz = 16384,
		.task = fish_bbox_npu_test_task,
		.tskatr = TA_HLNG | TA_RNG3,
	};
	ID fish_task_id = tk_cre_tsk(&fish_task);

	if (fish_task_id <= 0 || tk_sta_tsk(fish_task_id, 0) != E_OK)
	{
		tm_putstring((UB*)"Failed to start fish model test task.\n");
		return -1;
	}

	tk_slp_tsk(TMO_FEVR);

	return 0;
}
