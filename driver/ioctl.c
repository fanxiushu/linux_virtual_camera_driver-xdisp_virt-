//// by fanxiushu 2024-11-16

#include "avstrm_virt.h"

#define MIN_KVER  KERNEL_VERSION(2,6,36)

static int avs_cdo_open(struct inode* ino, struct file* fp);
static int avs_cdo_release(struct inode* ino, struct file* fp);

#if LINUX_VERSION_CODE >= MIN_KVER
static long avs_cdo_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);
#else
static long avs_cdo_ioctl(struct inode* inode, struct file *fp, unsigned int cmd, unsigned long arg);
#endif
static unsigned int avs_cdo_poll(struct file *fp, poll_table *wait);

static ssize_t avs_cdo_read(struct file* fp, char* buf, size_t length, loff_t* offset);
static ssize_t avs_cdo_write(struct file* fp, const char* buf, size_t length, loff_t* offset);

static int avs_camera_data_transfer(
	struct avs_camera_devctx* ctx, const char* buf);

///////
static struct file_operations avs_cdo_fops = {
	.open = avs_cdo_open,
	.release = avs_cdo_release,
	.read = avs_cdo_read,
	.write = avs_cdo_write,
#if LINUX_VERSION_CODE >= MIN_KVER
	.unlocked_ioctl = avs_cdo_ioctl,
#else
	.ioctl = avs_cdo_ioctl,
#endif
	.poll = avs_cdo_poll,
};

/////以下版本号不一定准确,请根据自己版本情况做调整
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,160)
static char* cdo_devnode_callback(struct device *dev, umode_t *mode)
#else
static char* cdo_devnode_callback(const struct device *dev, umode_t *mode)
#endif
{
	if (mode) {
		*mode = 0666; //// 设置任何用户读写
	}
	return NULL;
}
int cdo_init(void)
{
	int result;
	dev_t devt;
	struct device* coll_dev;

	if (avs->cdo_devid) {
		result = register_chrdev_region(avs->cdo_devid, 1, CDO_NAME);
	}
	else {
		result = alloc_chrdev_region(&avs->cdo_devid, 0, 1, CDO_NAME);

	}
	if (result < 0) {
		printk(KERN_ALERT"avs: register Device ID Error.\n");
		return result;
	}
	devt = avs->cdo_devid;

	/////
	cdev_init(&avs->cdo_dev, &avs_cdo_fops); ////
	avs->cdo_dev.owner = THIS_MODULE;
	avs->cdo_dev.ops = &avs_cdo_fops;

	result = cdev_add(&avs->cdo_dev, devt, 1);
	if (result < 0) {
		printk("avs: cdev_add err=%d\n", result);
		unregister_chrdev_region(devt, 1);
		return result;
	}

	///创建设备节点，为了用户程序能访问
	avs->cdo_cls = CLASS_CREATE( "cls_avstrm");
	if (IS_ERR(avs->cdo_cls)) {
		///
		result = PTR_ERR(avs->cdo_cls);
		avs->cdo_cls = NULL;
		printk(KERN_ALERT"avs: class_create err =%d\n", result);
		goto E;
	}
	avs->cdo_cls->devnode = cdo_devnode_callback; //设置字符设备的读写属性, 把字符设备设置为任何用户都可读写的属性

	coll_dev = device_create(avs->cdo_cls, NULL, devt, NULL, "%s", CDO_NAME);
	if (IS_ERR(coll_dev)) {
		result = PTR_ERR(coll_dev);
		printk(KERN_ALERT"avs: device_create err=%d\n", result);
		goto E;
	}
	printk("--- create avstream camera, user io device ok.\n");
	return 0;
	//////
E:
	if (avs->cdo_cls) {
		device_destroy(avs->cdo_cls, devt);
		class_destroy(avs->cdo_cls);
		avs->cdo_cls = NULL;
	}
	cdev_del(&avs->cdo_dev);
	unregister_chrdev_region(devt, 1);

	return result;
}

void cdo_deinit(void)
{
	dev_t devt;
	devt = avs->cdo_devid;

	if (avs->cdo_cls) {
		//
		// 2019-10-31, fixed bug, 以前是在 cdev_del之后调用，会死机, kernel 4.19
		device_destroy(avs->cdo_cls, devt);
		class_destroy(avs->cdo_cls);
		avs->cdo_cls = NULL;

		cdev_del(&avs->cdo_dev);
		unregister_chrdev_region(devt, 1);
	}
	//////
}

//////
///字符设备读写
static int avs_cdo_open(struct inode* ino, struct file* fp)
{
	///
	fp->private_data = NULL; /// ->  

	printk(KERN_NOTICE"avs_cdo_open \n");
	return 0;
}

static int avs_cdo_release(struct inode* ino, struct file* fp)
{
	printk("avs_cdo_release \n");
	struct avs_camera_devctx* cd = (struct avs_camera_devctx*)fp->private_data;
	fp->private_data = NULL;
	if (cd) {
		avs_camera_destroy(cd); /// destroy
	}
	return 0;
}


#if LINUX_VERSION_CODE >= MIN_KVER
static long avs_cdo_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
#else
static long avs_cdo_ioctl(struct inode* inode, struct file *fp, unsigned int cmd, unsigned long arg)
#endif
{
	/////
	int ret = 0;
	struct avs_camera_devctx* cd = (struct avs_camera_devctx*)fp->private_data;

	if (_IOC_TYPE(cmd) != IOCTL_MAGIC) return -EINVAL;

	switch (cmd)
	{
	case IOCTL_CHECK_DRIVER_INSTALLED://
		{
		    int installed = 2; /// 安装了驱动，但是只实现了虚拟摄像头
			if (copy_to_user((const char*)arg, &installed, sizeof(int)) != 0)return -EFAULT;
			////
		}
		break;

	case IOCTL_CREATE_CAMERA://创建虚拟摄像头
		{
			/////
		    char pbuf[sizeof(struct ioctl_camera_create_t)
				+ MAX_FRAMESIZES_COUNT * sizeof(struct ioctl_framesize_t)];
			struct ioctl_camera_create_t* ct  = (struct ioctl_camera_create_t*)pbuf;
			if (copy_from_user(ct, (const char*)arg, sizeof(*ct)) != 0) return -EFAULT;
			ct->name[sizeof(ct->name) - 1] = '\0';

			if (ct->framesizes_count < 0 || ct->framesizes_count > MAX_FRAMESIZES_COUNT) return -EINVAL;
			if (ct->framesizes_count > 0) {
				if (copy_from_user( (ct + 1), (const char*)arg + sizeof(*ct), 
					ct->framesizes_count*sizeof(struct ioctl_framesize_t) ) != 0) return -EFAULT;
			}

			///////
			if (cd) {// exist, update name
				/////
				avs_camera_modify_param(cd, ct);

				/////////
			}
			else {
				/////
				ret = avs_camera_create(ct, &cd);

				fp->private_data = cd; // save ptr
				////
			}
		}
		break;

	case IOCTL_SET_CAMERA_LOGO:
		if (cd) {
			////
			struct ioctl_avtrans_header_t hdr;
			char* b;
			if (copy_from_user(&hdr, (const char*)arg, sizeof(hdr)) != 0) return -EFAULT;
			if (hdr.camera.width <= 0 || hdr.camera.height <= 0 ||
				(hdr.camera.width * hdr.camera.height*3 != hdr.camera.data_length )) {// not RGB24
				return -EINVAL;
			}
			b = vmalloc(hdr.camera.data_length);
			if (!b)return -ENOMEM;
			if (copy_from_user(b, (char*)arg + sizeof(hdr), hdr.camera.data_length ) != 0) {
				vfree(b);
				return -EFAULT;
			}

			m_lock(cd);
			if (cd->logo_image_buffer)vfree(cd->logo_image_buffer);
			cd->logo_image_buffer = b;
			cd->logo_image_width = hdr.camera.width;
			cd->logo_image_height = hdr.camera.height;
			if (cd->video_image_buffer) {
				gen_yuy2_logo_picture(cd, cd->video_width, cd->video_height, cd->video_image_buffer);
			}
			m_unlock(cd);
		}
		else {
			ret = -ENODEV;
		}
		break;

	case IOCTL_GET_CAMERA_INFO:
		if (cd) {
			/////
			struct ioctl_camera_information_t info;

			m_lock(cd);
			info.video_width = cd->video_width;
			info.video_height = cd->video_height;
			info.frame_fps = 1000/cd->sleep_msec;
			info.stream_start = cd->stream_start_count;
			info.framesizes_count = cd->frame_sizes_count;

			if (copy_to_user((const char*)arg, &info, sizeof(info)) != 0) {
				m_unlock(cd);
				return -EFAULT;
			}
			if (copy_to_user((const char*)arg + sizeof(info), cd->frame_sizes, 
				cd->frame_sizes_count * sizeof(struct ioctl_framesize_t)) != 0) {
				m_unlock(cd);
				return -EFAULT;
			}
			/////
			m_unlock(cd);
		}
		else {
			ret = -ENODEV;
		}
		break;

	case IOCTL_DATA_TRANSFER:
		if (cd) ret = avs_camera_data_transfer(cd, (const char*)arg);
		else ret = -ENODEV;
		break;

	default:
		ret = -EINVAL;
	}
	return ret;
}

static unsigned int avs_cdo_poll(struct file *fp, poll_table *wait)
{
	struct avs_camera_devctx* cd = (struct avs_camera_devctx*)fp->private_data;
	////

	return 0;
}


static ssize_t avs_cdo_read(struct file* fp, char* buf, size_t length, loff_t* offset)
{
	struct avs_camera_devctx* cd = (struct avs_camera_devctx*)fp->private_data;
	/////
	
	return 0; ////
}

static ssize_t avs_cdo_write(struct file* fp, const char* buf, size_t length, loff_t* offset)
{
	int ret = 0;
	struct avs_camera_devctx* ctx = (struct avs_camera_devctx*)fp->private_data;
	///////
	return 0;
}

////////
static int avs_camera_data_transfer(
	struct avs_camera_devctx* ctx, const char* buf)
{
	int ret = 0;
	struct ioctl_avtrans_header_t hdr;

	if (!ctx)return -ENODEV;

	if (copy_from_user(&hdr, buf, sizeof(hdr)) != 0)return -EFAULT;
	
	if (hdr.type != 1) return -EINVAL;// only camera video 

	if ( hdr.camera.width*hdr.camera.height*BIT_COUNT / 8 != hdr.camera.data_length ) {
		/////
		return -EINVAL;
	}

	m_lock(ctx);

	if (hdr.camera.width != ctx->video_width || hdr.camera.height != ctx->video_height) {/// not match
		m_unlock(ctx);
		return -ERANGE; /// not match
	}
	if (!ctx->video_image_buffer) {
		int size = ctx->video_width*ctx->video_height*BIT_COUNT / 8;
		ctx->video_image_buffer = (char*)vmalloc(size);
		if (!ctx->video_image_buffer) {
			m_unlock(ctx);
			return -ENOMEM;
		}
	}
	////
	ret = copy_from_user(ctx->video_image_buffer, buf + sizeof(hdr), hdr.camera.data_length);

	m_unlock(ctx);

	if (ret != 0) return -EFAULT;
	/////
	return 0;
}

