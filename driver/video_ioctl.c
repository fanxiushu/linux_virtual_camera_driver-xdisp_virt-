/// by fanxiushu 2024-11-17

#include "avstrm_virt.h"

/////

static int vidioc_querycap(struct file *file, void  *priv,
	struct v4l2_capability *cap)
{
	struct avs_camera_devctx *ctx = video_drvdata(file);
	printk("-- vidioc_querycap \n");

	m_lock(ctx);
	snprintf(cap->driver, sizeof(cap->driver), "%s", "avstrm_virt");// ctx->name);
	snprintf(cap->card, sizeof(cap->card), "%s", ctx->name);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:avstrm_virt-%03d", ctx->inst_id);
	m_unlock(ctx);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	ctx->dev.device_caps =
#endif
		cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;// | V4L2_CAP_READWRITE;
		
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	memset(cap->reserved, 0, sizeof(cap->reserved));
	return 0;
}

///枚举所有的格式，这里只有一个YUY2，所以只填充一个
static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
	struct v4l2_fmtdesc *f)
{
	printk("-- vidioc_enum_fmt_vid_cap index=%d\n", f->index);
	if (f->index >= 1 )
		return -EINVAL;

	strncpy(f->description, "4:2:2, packed, YUY2", sizeof(f->description));
	f->pixelformat = PIX_FMT; // YUY2
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct avs_camera_devctx *ctx = video_drvdata(file);

	m_lock(ctx);
	f->fmt.pix.width = ctx->video_width;
	f->fmt.pix.height = ctx->video_height;

	f->fmt.pix.field = V4L2_FIELD_NONE;// V4L2_FIELD_INTERLACED;

	f->fmt.pix.pixelformat = PIX_FMT; // YUY2

	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * BIT_COUNT) >> 3;

	f->fmt.pix.sizeimage =
	    f->fmt.pix.height * f->fmt.pix.bytesperline;
	
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB; // V4L2_COLORSPACE_SMPTE170M; // YUY2, RGB: 
	printk("-- vidioc_g_fmt_vid_cap: w=%d, h=%d\n", ctx->video_width, ctx->video_height);
	m_unlock(ctx);
	return 0;
}

///测试和尝试获取一个格式,
static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
	struct v4l2_format *f)
{
	int i, dw, dh, dwh = 0x7fffffff;
	struct avs_camera_devctx *ctx = video_drvdata(file);
	struct ioctl_framesize_t* found = NULL;
	///////
	if (f->fmt.pix.pixelformat != PIX_FMT) {
		/////
		char *m = (char*)&f->fmt.pix.pixelformat;
		printk("** vidioc_try_fmt_vid_cap: fmt.pix.pixelformat=%c%c%c%c, not V4L2_PIX_FMT_YUYV \n", m[0],m[1],m[2],m[3]);
		return -EINVAL;
	}

	if (f->fmt.pix.field == V4L2_FIELD_ANY) {
        f->fmt.pix.field = V4L2_FIELD_NONE;
	}
	else if (f->fmt.pix.field != V4L2_FIELD_NONE) {
		printk("** vidioc_try_fmt_vid_cap: f->fmt.pix.field=%d, not V4L2_FIELD_NONE \n", f->fmt.pix.field);
		return -EINVAL;
	}
	
	///////查找匹配的w和h
	m_lock(ctx);
	for (i = 0; i < ctx->frame_sizes_count; ++i) {
		struct ioctl_framesize_t* ss = &ctx->frame_sizes[i];
		dw = f->fmt.pix.width - ss->width; if (dw < 0)dw = -dw;
		dh = f->fmt.pix.height - ss->height; if (dh < 0)dh = -dh;
		if (dw + dh < dwh) {//找到更接近的
			dwh = dw + dh;
			found = ss;
		}
	}
	
	////
	if (found) {
		f->fmt.pix.width = found->width;
		f->fmt.pix.height = found->height;
	}
	else {
		///在没有匹配的情况下，保持不变,反正是虚拟摄像头，size可以匹配
		if (f->fmt.pix.width < 160)f->fmt.pix.width = 160; else if (f->fmt.pix.width > 8000)f->fmt.pix.width = 8000;
		if (f->fmt.pix.height < 120)f->fmt.pix.height = 120; else if (f->fmt.pix.height > 8000)f->fmt.pix.height = 8000;
		////
		printk("-- warning: vidioc_try_fmt_vid_cap: keep w=%d, h=%d\n", f->fmt.pix.width, f->fmt.pix.height);
	}
	m_unlock(ctx);
	////

	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * BIT_COUNT) >> 3;

	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB; // V4L2_COLORSPACE_SMPTE170M;// yuy2
	
	f->fmt.pix.priv = 0;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct avs_camera_devctx *dev = video_drvdata(file);
	struct vb2_queue *q = &dev->vb2_q;
	bool chg = false;

	if (dev->is_destroy) {
		printk("** vidioc_s_fmt_vid_cap: camera had closed.\n");
		return -EINVAL;
	}
	int ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret < 0) {
		printk("--- vidioc_s_fmt_vid_cap: check format err=%d\n", ret );
		return ret;
	}

	if (dev->stream_start_count) {
		printk("--- vidioc_s_fmt_vid_cap : stream on, must off first.\n");
		return -EBUSY;
	}

	////
	m_lock(dev);
	printk("--- vidioc_s_fmt_vid_cap: YUY2, w=%d, h=%d\n", f->fmt.pix.width, f->fmt.pix.height );
	if (dev->video_width != f->fmt.pix.width || dev->video_height != f->fmt.pix.height) {
		chg = true;
		printk("--- set format: w/h, had changed.\n");
	}

	/////vidioc_try_fmt_vid_cap 里边已经对width和height做了调整
	dev->video_width = f->fmt.pix.width;
	dev->video_height = f->fmt.pix.height;

	if ( chg || !dev->video_image_buffer) {
		/////
		int size = dev->video_width*dev->video_height*BIT_COUNT / 8;

		if (dev->video_image_buffer)vfree(dev->video_image_buffer);
		////
		dev->video_image_buffer = (char*)vmalloc(size);

		if (dev->video_image_buffer) { /// gen logo image 
			////
			if (dev->logo_image_buffer) {
				gen_yuy2_logo_picture(dev, dev->video_width, dev->video_height, dev->video_image_buffer);
			}
			else {
				memset(dev->video_image_buffer, 0, size); /// no logo , init 0
			}
			///////
		}

		///////
		if (dev->efd_ctx) EVENTFD_SIGNAL(dev->efd_ctx);

		////
	}
	m_unlock(dev);

	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
	struct v4l2_frmsizeenum *fsize)
{
	///采用 V4L2_FRMSIZE_TYPE_DISCRETE 表示设置某些固定的width和height
	///应该还有逐步递进V4L2_FRMSIZE_TYPE_STEPWISE的办法来描述 width和height，
	printk("-- vidioc_enum_framesizes, index=%d\n", fsize->index);
	struct avs_camera_devctx *ctx = video_drvdata(file);

	if (fsize->pixel_format != PIX_FMT) {
		return -EINVAL;
	}

	m_lock(ctx);
	if (fsize->index >= ctx->frame_sizes_count) {
		m_unlock(ctx);
		return -EINVAL;
	}
	
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = ctx->frame_sizes[fsize->index].width;
	fsize->discrete.height = ctx->frame_sizes[fsize->index].height;
	m_unlock(ctx);

	return 0;
}
static int vidioc_enum_frameintervals(struct file *file, void *priv,
	struct v4l2_frmivalenum *ival)
{
	int i =0 ;
	struct avs_camera_devctx *ctx = video_drvdata(file);
	printk("-- vidioc_enum_frameintervals index=%d\n", ival->index);
	
	////
	if (ival->index !=0 )
		return -EINVAL;

	if (ival->pixel_format != PIX_FMT) {
		return -EINVAL;
	}
	///本来还想做width和height匹配判断的，不过这里不判断了

#if 0
	ival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	ival->stepwise.min.numerator = 1;
	ival->stepwise.min.denominator = 25;
	ival->stepwise.max.numerator = 25;
	ival->stepwise.max.denominator = 1;
	ival->stepwise.step.numerator = 1;
	ival->stepwise.step.denominator = 1;
#else
	///很奇怪的， 6.11 内核版本 ubbunt24， 
	///cheese，在这里得设置 V4L2_FRMIVAL_TYPE_DISCRETE，并且max fps 不能超过30 ，否则就会出错
	ival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	ival->discrete.numerator = 1;
	ival->discrete.denominator = 30; // max fps
#endif

	return 0;
}

/// one input
static int vidioc_enum_input(struct file *file, void *priv,
	struct v4l2_input *inp)
{
	printk("-- vidioc_enum_input index=%d\n", inp->index);
	if (inp->index >= 1 )
		return -EINVAL;

	memset(inp, 0, sizeof(*inp));
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	sprintf(inp->name, "Camera %u", inp->index + 1);

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	printk("-- vidioc_g_input \n");
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct avs_camera_devctx *dev = video_drvdata(file);
	printk("-- vidioc_s_input\n");
	if (i >= 1 )
		return -EINVAL;

	if (i == 0 )
		return 0;

	return 0;
}

/// get/set fps
static int vidioc_g_parm(struct file *file, void *fh,
	struct v4l2_streamparm *parm)
{
	struct avs_camera_devctx *ctx = video_drvdata(file);
	printk("-- vidioc_g_parm\n");
	/////
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) return -EINVAL;

	//////
	m_lock(ctx);
	memset(parm, 0, sizeof(*parm));
	parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.capturemode = 0;
	parm->parm.capture.timeperframe.numerator = 1;
	parm->parm.capture.timeperframe.denominator = 1000 / ctx->sleep_msec;
	parm->parm.capture.extendedmode = 0;
	parm->parm.capture.readbuffers = 1;
	m_unlock(ctx);

	return 0;
}

static int vidioc_s_parm(struct file *file, void *fh,
	struct v4l2_streamparm *parm)
{
	struct avs_camera_devctx *ctx = video_drvdata(file);
	struct vb2_queue *q = &ctx->vb2_q;
	printk("-- vidioc_s_parm \n");
	/////
	if (ctx->stream_start_count) {
		printk("--- vidioc_s_parm : stream on, must off first.\n");
		return -EBUSY;
	}

//	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) return -EINVAL;
	m_lock(ctx);
	if (parm->parm.capture.timeperframe.numerator > 0 && parm->parm.capture.timeperframe.denominator > 0) {
		int fps = parm->parm.capture.timeperframe.denominator / parm->parm.capture.timeperframe.numerator;
		if (fps > 0 && fps <= 1000 ) {
			ctx->sleep_msec = 1000 / fps;
			printk("-- vidioc_s_parm: Set sleep_msec=%d ms\n", ctx->sleep_msec );
		}
	}
	m_unlock(ctx);
	return 0;
}

static int vidioc_g_selection(struct file *file, void *fh,
	struct v4l2_selection *sel)
{
	struct avs_camera_devctx *ctx = video_drvdata(file);
	printk("-- vidioc_g_selection \n");
	////
	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP_DEFAULT && sel->target != V4L2_SEL_TGT_CROP_BOUNDS) {
		printk("** vidioc_g_selection , sel->target invalid.\n");
		return -EINVAL;
	}

	m_lock(ctx);
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = ctx->video_width;
	sel->r.height = ctx->video_height;
	m_unlock(ctx);

	return 0;
}

//////
static int avs_fop_open(struct file *file)
{
	int ret;
	struct avs_camera_devctx *ctx = video_drvdata(file);
	int pid= pid_nr(get_task_pid(current, PIDTYPE_PID));

	ret = avs_camera_alloc(ctx);
	if (ret) {
		printk("** v4l2_file_operations->open: camera source had closed. PID=%d, user_count=%d\n", pid, ctx->user_count );
		return ret;
	}

	printk("@@ v4l2_file_operations->open, PID=%d, user_count=%d\n",pid, ctx->user_count );

	ret = v4l2_fh_open(file);

	if (ret < 0) {
		///
		avs_camera_free(ctx);
	}
	return ret;
}
static int avs_fop_release(struct file *file)
{
	int ret;
	struct avs_camera_devctx *ctx = video_drvdata(file);
	int pid = pid_nr(get_task_pid(current, PIDTYPE_PID));

	ret = vb2_fop_release(file);
	printk("## v4l2_file_operations->release, PID=%d, user_count=%d\n", pid, ctx->user_count - 1 );

	avs_camera_free(ctx);

	return ret;
}
static const struct v4l2_file_operations avs_fops = {
	///
	.owner = THIS_MODULE,
	.open = avs_fop_open,
	.release = avs_fop_release,
	///以下全是系统函数
	.read = vb2_fop_read,
	.write = vb2_fop_write,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2, //此函数会调用下面的avs_ioctl_ops的接口函数
	.mmap = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops avs_ioctl_ops = {
	//////
	.vidioc_querycap = vidioc_querycap,                  // 查询设备能力
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,  //枚举所有支持的format 格式
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,        // 获取当前的format格式
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,    //检测是否支持某个format
 	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,        //设置 format
	.vidioc_enum_framesizes = vidioc_enum_framesizes,    // 枚举所有支持的分辨率
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals, ///枚举支持的FPS, 需要设置这个函数，否则某些程序比如cheese无法正常
	.vidioc_enum_input = vidioc_enum_input,              //枚举输入
	.vidioc_g_input = vidioc_g_input,                    // 获取当前输入
	.vidioc_s_input = vidioc_s_input,                    //设置当前输入
	.vidioc_g_parm = vidioc_g_parm,                      //获取当前参数，主要是 FPS
	.vidioc_s_parm = vidioc_s_parm,                      //设置当前参数，主要是 FPS
//	.vidioc_g_selection = vidioc_g_selection,            //

	/////下面填充全是系统函数
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,	
	.vidioc_expbuf = vb2_ioctl_expbuf, // 添加此函数，否则某些版本无法正常使用cheese
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
//	.vidioc_remove_bufs = vb2_ioctl_remove_bufs,

	.vidioc_log_status = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static void avs_video_device_release(struct video_device *vfd)
{
	struct avs_camera_devctx* ctx = video_get_drvdata(vfd);
	////
	printk("-- video device Release.\n");
	avs_camera_release(ctx);
}
int avs_video_device_init(struct avs_camera_devctx* ctx)
{
	int ret, type;
	struct video_device *vfd;

	vfd = &ctx->dev;

	/////
	snprintf(vfd->name ,sizeof(vfd->name),  "avstrm_virt-%03d", ctx->inst_id );
	vfd->fops = &avs_fops;
	vfd->ioctl_ops = &avs_ioctl_ops;
	vfd->release = avs_video_device_release; ///因为是 静态分配的video_device， 所以不用释放

	/////
	vfd->v4l2_dev = &ctx->v4l2_dev;

	vfd->queue = &ctx->vb2_q; /// 已经初始化

	vfd->lock = &ctx->v4l2_mutex; //用于加锁 fops和ioctl_fops操作

	video_set_drvdata(vfd, ctx); /// 

	////以下版本号不一定准确,请根据自己版本情况做调整
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)
	type = 0;// VFL_TYPE_GRABBER;
	set_bit(V4L2_FL_USE_FH_PRIO, &vfd->flags); ///

#else
	type = 0;// VFL_TYPE_VIDEO;
	vfd->vfl_dir = VFL_DIR_RX;

	/// 高版本 kernel 必须这个参数，否则会crash
	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;// | V4L2_CAP_READWRITE;

#endif

	//////
	ret = video_register_device(vfd, type, -1); // -1 auto generate /dev/videoX
	if (ret) {
		printk("** video_register_device err=%d\n", ret);

		return ret;
	}

	return 0;
}

