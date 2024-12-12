/// by fanxiushu 2024-11-16
#include "avstrm_virt.h"
//////
struct avs_virt_t __gbl_avs;

//////
static int avs_init(void)
{
	memset(avs, 0, sizeof(*avs));
	///
	spin_lock_init(&avs->spinlock);
	INIT_LIST_HEAD(&avs->lostsrc_cameras);

	return 0;
}
static void avs_deinit(void)
{
	////
}

static int __init avs_driver_init(void)
{
	/////
	int err;
	err = avs_init();
	if (err)return err;

	err = cdo_init();
	if (err) {
		avs_deinit();
		return err;
	}

	//////
	return 0;
}

static void __exit avs_driver_exit(void)
{
	cdo_deinit();

	avs_deinit();
	////
}

////
module_init(avs_driver_init);
module_exit(avs_driver_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(AVS_VERSION);

