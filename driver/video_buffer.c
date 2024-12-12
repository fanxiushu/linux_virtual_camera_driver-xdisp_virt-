/// by fanxiushu 2024-11-17
#include "avstrm_virt.h"

///
#define CHK_INVALID_RETURN(dev, str) \
	sp_lock(dev); \
	if (dev->is_destroy) { \
		sp_unlock(dev); printk("** camera source had closed. in [%s]\n", str);\
		return -EINVAL; \
	} \
	sp_unlock(dev);

//////以下版本号不一定准确,请根据自己版本情况做调整
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
	unsigned int *nbuffers, unsigned int *nplanes,
	unsigned int sizes[], void *alloc_ctxs[])
#else
static int queue_setup(struct vb2_queue *vq,
	unsigned int *nbuffers,
	unsigned int *nplanes, unsigned int sizes[],
	struct device *alloc_devs[])
#endif
{
	struct avs_camera_devctx *dev = vb2_get_drv_priv(vq);
	unsigned long size;
	printk("-- queue_setup \n");
	
	CHK_INVALID_RETURN(dev,"queue_setup"); ////

	m_lock(dev);
	size = dev->video_width * dev->video_height * BIT_COUNT/8;
	m_unlock(dev);

	if (size == 0)
		return -EINVAL;

	if (*nbuffers < 2)
		*nbuffers = 16;

	*nplanes = 1; // YUY2 只有一个 plane

	sizes[0] = size;

	return 0;
}

/////////////////////////////////////////////

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct avs_camera_devctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
	struct avs_video_buffer *buf = container_of(vb, struct avs_video_buffer, buffer.vb2_buf);
#else
	struct avs_video_buffer *buf = container_of(vb, struct avs_video_buffer, buffer);
#endif
	/////
	unsigned long size;
	m_lock(ctx);
	size = ctx->video_width*ctx->video_height*BIT_COUNT / 8;
	m_unlock(ctx);
	if (vb2_plane_size(vb, 0) < size) {
		printk("%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload( vb, 0, size);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct avs_camera_devctx *dev = vb2_get_drv_priv(vb->vb2_queue);
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
	struct avs_video_buffer *buf = container_of(vb, struct avs_video_buffer, buffer.vb2_buf);
#else
	struct avs_video_buffer *buf = container_of(vb, struct avs_video_buffer, buffer);
#endif

//	printk("-- buffer queue\n");
	////
	sp_lock(dev);
	if (dev->is_destroy ) {/// had destroyed 
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
		vb2_buffer_done(&buf->buffer.vb2_buf, VB2_BUF_STATE_ERROR);
#else
		vb2_buffer_done(&buf->buffer, VB2_BUF_STATE_ERROR);
#endif
	//	printk("*** vb2 buffer_queue: avs camera destroyed, complete Err\n");
	}
	else {
		list_add_tail(&buf->list, &dev->video_buffer_head);
	}
	sp_unlock(dev);
}

static void avs_process_frame(struct avs_camera_devctx *ctx)
{
	struct avs_video_buffer* buf;
	///
	sp_lock(ctx);
	if (list_empty(&ctx->video_buffer_head)) {
		sp_unlock(ctx);
		return;
	}
	buf = list_entry(ctx->video_buffer_head.next, struct avs_video_buffer, list);
	list_del(&buf->list);
	sp_unlock(ctx);

	////
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
	void *vbuf = vb2_plane_vaddr(&buf->buffer.vb2_buf, 0); /// plane 0
#else
	void *vbuf = vb2_plane_vaddr(&buf->buffer, 0); /// plane 0
#endif
	
	m_lock(ctx);
	if (ctx->video_image_buffer) {// fill buffer
		/////
		memcpy(vbuf, ctx->video_image_buffer, ctx->video_width* ctx->video_height*BIT_COUNT / 8);

	}
	///以下版本号不一定准确,请根据自己版本情况做调整
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
	buf->buffer.vb2_buf.timestamp = ktime_get_ns(); // 需要设置正确的时间戳，否则像某些程序比如VLC会出现PCR时序错误，
	buf->buffer.sequence = ctx->sequence++;  /// 需要设置这个，并且递增，否则cheese无法正常显示
	buf->buffer.field = V4L2_FIELD_NONE;
	vb2_set_plane_payload(&buf->buffer.vb2_buf, 0,
		ctx->video_width* ctx->video_height*BIT_COUNT / 8 );

#elif LINUX_VERSION_CODE > KERNEL_VERSION(3,10,0)
	buf->buffer.timestamp = ktime_get_ns(); // 需要设置正确的时间戳，否则像某些程序比如VLC会出现PCR时序错误，
#endif

	m_unlock(ctx);

	//// done
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
	vb2_buffer_done(&buf->buffer.vb2_buf, VB2_BUF_STATE_DONE);
#else
	vb2_buffer_done(&buf->buffer, VB2_BUF_STATE_DONE);
#endif
}
static int avs_kthread_func(void*p)
{
	struct avs_camera_devctx *ctx = p;
	////
	set_freezable();
//	unsigned long t1, t2, t3,t4;
	while (true) {
		int timeout; //t1 = jiffies;
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue(&ctx->wait_q, &wait);
		if (kthread_should_stop()) {
			remove_wait_queue(&ctx->wait_q, &wait);
			try_to_freeze();
			break;
		}
		//t2 = jiffies;
		///// copy data
		avs_process_frame(ctx); //t3 = jiffies;
		
		////// sleep msec
		timeout = msecs_to_jiffies(ctx->sleep_msec); ////
		schedule_timeout_interruptible(timeout); //t4 = jiffies;

		remove_wait_queue(&ctx->wait_q, &wait);
		try_to_freeze();
		////
//		printk("dt1=%d, dt2=%d, dt3=%d ;a_dt=%d\n", jiffies_to_msecs(t2 - t1), jiffies_to_msecs(t3 - t2), jiffies_to_msecs(t4 - t3), jiffies_to_msecs(jiffies - t1));
	}
	printk("--- avs_kthread_func quit.\n");
	return 0;
}

static inline void 
inc_or_dec_stream_count(struct avs_camera_devctx *ctx, bool is_inc)
{
	sp_lock(ctx);
	if(is_inc)++ctx->stream_start_count;
	else --ctx->stream_start_count;
	sp_unlock(ctx);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct avs_camera_devctx *ctx = vb2_get_drv_priv(vq);

	ctx->sequence = 0;
	////
	CHK_INVALID_RETURN(ctx,"start_steaming");

	inc_or_dec_stream_count(ctx, true);

	//////
	printk("-- start_streaming \n");
	ctx->kthread = kthread_run(avs_kthread_func, ctx, ctx->name);
	if (IS_ERR(ctx->kthread)) {
		int ret = PTR_ERR(ctx->kthread);
		printk(KERN_ALERT"** kthread_run err=%d\n", ret);
		ctx->kthread = NULL;
		
		inc_or_dec_stream_count(ctx, false);

		return ret;
	}

	////
	wake_up_interruptible(&ctx->wait_q);

	evt_signal_withlock(ctx);

	return 0;
}

static int stop_streaming(struct vb2_queue *vq)
{
	struct avs_camera_devctx *ctx = vb2_get_drv_priv(vq);
	
	////	
	if (ctx->is_destroy) {
		inc_or_dec_stream_count(ctx, false);

		evt_signal_withlock(ctx);

		printk("-- stop streaming: camera had closed.\n");
		return -ENODEV;
	}

	printk("-- stop_streaming \n");
	//// 
	avs_camera_queue_cleanup(ctx);

	///////	
	evt_signal_withlock(ctx);

	/////
	inc_or_dec_stream_count(ctx, false);

	return 0;
}

/////
static struct vb2_ops avs_vb2_ops = {
	.queue_setup = queue_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.start_streaming = start_streaming,
	/////以下版本号不一定准确,请根据自己版本情况做调整
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
	.stop_streaming = stop_streaming,
#else
	.stop_streaming = (void (*)(struct vb2_queue *))stop_streaming,
#endif
	///系统提供，在 vb2_queue.lock 设置mutex才能使用
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

int avs_vb2_queue_init(struct avs_camera_devctx* ctx)
{
	int ret;
	struct vb2_queue *q;

	q = &ctx->vb2_q;

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->drv_priv = ctx; ////
	q->buf_struct_size = sizeof(struct avs_video_buffer);
	q->ops = &avs_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->lock = &ctx->v4l2_mutex; //// 设置锁， 
#if LINUX_VERSION_CODE == KERNEL_VERSION(3,10,0) 
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
#elif LINUX_VERSION_CODE > KERNEL_VERSION(3,10,0)//这个版本号不一定准确，根据实际编译出错情况调整
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
#endif

	ret = vb2_queue_init(q);
	if (ret) {
		printk(KERN_ALERT"vb2_queue_init err=%d\n", ret);

		return ret;
	}

	return 0;
}

