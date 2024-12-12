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

///ö�����еĸ�ʽ������ֻ��һ��YUY2������ֻ���һ��
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

///���Ժͳ��Ի�ȡһ����ʽ,
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
	
	///////����ƥ���w��h
	m_lock(ctx);
	for (i = 0; i < ctx->frame_sizes_count; ++i) {
		struct ioctl_framesize_t* ss = &ctx->frame_sizes[i];
		dw = f->fmt.pix.width - ss->width; if (dw < 0)dw = -dw;
		dh = f->fmt.pix.height - ss->height; if (dh < 0)dh = -dh;
		if (dw + dh < dwh) {//�ҵ����ӽ���
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
		///��û��ƥ�������£����ֲ���,��������������ͷ��size����ƥ��
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

	/////vidioc_try_fmt_vid_cap ����Ѿ���width��height���˵���
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
	///���� V4L2_FRMSIZE_TYPE_DISCRETE ��ʾ����ĳЩ�̶���width��height
	///Ӧ�û����𲽵ݽ�V4L2_FRMSIZE_TYPE_STEPWISE�İ취������ width��height��
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
	///����������width��heightƥ���жϵģ��������ﲻ�ж���

#if 0
	ival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	ival->stepwise.min.numerator = 1;
	ival->stepwise.min.denominator = 25;
	ival->stepwise.max.numerator = 25;
	ival->stepwise.max.denominator = 1;
	ival->stepwise.step.numerator = 1;
	ival->stepwise.step.denominator = 1;
#else
	///����ֵģ� 6.11 �ں˰汾 ubbunt24�� 
	///cheese������������� V4L2_FRMIVAL_TYPE_DISCRETE������max fps ���ܳ���30 ������ͻ����
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
	///����ȫ��ϵͳ����
	.read = vb2_fop_read,
	.write = vb2_fop_write,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2, //�˺�������������avs_ioctl_ops�Ľӿں���
	.mmap = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops avs_ioctl_ops = {
	//////
	.vidioc_querycap = vidioc_querycap,                  // ��ѯ�豸����
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,  //ö������֧�ֵ�format ��ʽ
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,        // ��ȡ��ǰ��format��ʽ
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,    //����Ƿ�֧��ĳ��format
 	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,        //���� format
	.vidioc_enum_framesizes = vidioc_enum_framesizes,    // ö������֧�ֵķֱ���
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals, ///ö��֧�ֵ�FPS, ��Ҫ�����������������ĳЩ�������cheese�޷�����
	.vidioc_enum_input = vidioc_enum_input,              //ö������
	.vidioc_g_input = vidioc_g_input,                    // ��ȡ��ǰ����
	.vidioc_s_input = vidioc_s_input,                    //���õ�ǰ����
	.vidioc_g_parm = vidioc_g_parm,                      //��ȡ��ǰ��������Ҫ�� FPS
	.vidioc_s_parm = vidioc_s_parm,                      //���õ�ǰ��������Ҫ�� FPS
//	.vidioc_g_selection = vidioc_g_selection,            //

	/////�������ȫ��ϵͳ����
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,	
	.vidioc_expbuf = vb2_ioctl_expbuf, // ��Ӵ˺���������ĳЩ�汾�޷�����ʹ��cheese
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
	vfd->release = avs_video_device_release; ///��Ϊ�� ��̬�����video_device�� ���Բ����ͷ�

	/////
	vfd->v4l2_dev = &ctx->v4l2_dev;

	vfd->queue = &ctx->vb2_q; /// �Ѿ���ʼ��

	vfd->lock = &ctx->v4l2_mutex; //���ڼ��� fops��ioctl_fops����

	video_set_drvdata(vfd, ctx); /// 

	////���°汾�Ų�һ��׼ȷ,������Լ��汾���������
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)
	type = 0;// VFL_TYPE_GRABBER;
	set_bit(V4L2_FL_USE_FH_PRIO, &vfd->flags); ///

#else
	type = 0;// VFL_TYPE_VIDEO;
	vfd->vfl_dir = VFL_DIR_RX;

	/// �߰汾 kernel ������������������crash
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

