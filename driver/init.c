// by fanxiushu 2024-11-16 
#include "avstrm_virt.h"

///设置固定分辨率
static struct ioctl_framesize_t framesize_Array[]=
{
	{1920,1080},
	{2560,1600},	
	{1600,900},
	{1280,720},
	{640,480},
	{320, 240},
};

//////
static int get_inst_id(void)
{
	int id = -1; int i;
	sp_lock(avs);
	for (i = 0; i < MAX_INSTANCE_COUNT; ++i) {
		if (!avs->inst_ids[i]) {
			id = i;
			avs->inst_ids[i] = 1;
			break;
		}
	}
	sp_unlock(avs);
	return id;
}
static void free_inst_id(int inst_id)
{
	if (inst_id >= 0 && inst_id < MAX_INSTANCE_COUNT) {
		sp_lock(avs);
		avs->inst_ids[inst_id] = 0;
		sp_unlock(avs);
	}
}

static int avs_camera_create_real(
	struct ioctl_camera_create_t* ct,
	struct avs_camera_devctx** p_ctx)
{
	int ret;
	struct avs_camera_devctx* ctx;
	struct v4l2_ctrl_handler *hdl;

	ctx = kmalloc(sizeof(struct avs_camera_devctx), GFP_KERNEL);
	if (!ctx) {
		return -ENOMEM;
	}
	memset( ctx, 0, sizeof(struct avs_camera_devctx) ); /// zero 

	spin_lock_init(&ctx->spinlock);

	mutex_init(&ctx->mutex); /// init mutex
	mutex_init(&ctx->v4l2_mutex);

	INIT_LIST_HEAD(&ctx->video_buffer_head); ///
	INIT_LIST_HEAD(&ctx->lostsrc_list);   ////清空，
	init_waitqueue_head(&ctx->wait_q);    /// wait_q;
	
	ctx->user_count = 1; ///把它设置1，表示数据源也占用一个计数

	if (ct->framesizes_count > 0 && ct->framesizes_count <= MAX_FRAMESIZES_COUNT) {
		memcpy(ctx->frame_sizes, ct + 1, ct->framesizes_count * sizeof(struct ioctl_framesize_t));
		ctx->frame_sizes_count = ct->framesizes_count;
	}
	else {
		memcpy(ctx->frame_sizes, framesize_Array, sizeof(framesize_Array));
	    ctx->frame_sizes_count = sizeof(framesize_Array) / sizeof(framesize_Array[0]);
	}
	
	ctx->video_width = ctx->frame_sizes[0].width;
	ctx->video_height = ctx->frame_sizes[0].height;

	memcpy(ctx->name, ct->name, sizeof(ct->name)); ///copy name
	if (ct->event_fd > 0) {
		ctx->efd_ctx = eventfd_ctx_fdget(ct->event_fd);
	}
	int fps = ct->fps;
	if (fps < 1)fps = 1; else if (fps > 1000)fps = 1000;
	ctx->sleep_msec = 1000/fps; 

	ctx->inst_id = get_inst_id();
	if (ctx->inst_id < 0) {
		printk("** not found idle inst id, maybe create much devices.\n");
		if (ctx->efd_ctx)eventfd_ctx_put(ctx->efd_ctx);
		kfree(ctx);
		return -EBUSY;
	}

	hdl = &ctx->ctrl_handler;

	//////NULL	
	snprintf( ctx->v4l2_dev.name, sizeof(ctx->v4l2_dev.name) ,
		"avstrm_virt-%03d", ctx->inst_id);
	ctx->v4l2_dev.name[sizeof(ctx->v4l2_dev.name) - 1] = '\0';

	ret = v4l2_device_register(NULL, &ctx->v4l2_dev); /// NULL, must fill 'v4l2_dev.name'
	if (ret) {
		printk(KERN_ALERT"v4l2_device_register err=%d\n", ret);
		if (ctx->efd_ctx)eventfd_ctx_put(ctx->efd_ctx);
		free_inst_id(ctx->inst_id);
		kfree(ctx);
		return ret;
	}
	
	v4l2_ctrl_handler_init(hdl, 1);
	///....  可以添加其他 ctrl
	if (hdl->error) {
		ret = hdl->error;
		if (ctx->efd_ctx)eventfd_ctx_put(ctx->efd_ctx);
		free_inst_id(ctx->inst_id);
		kfree(ctx);
		return ret;
	}
	ctx->v4l2_dev.ctrl_handler = hdl;

	/// init vb2 queue
	ret = avs_vb2_queue_init(ctx);
	if (ret) {
		v4l2_ctrl_handler_free(hdl);
		v4l2_device_unregister(&ctx->v4l2_dev);
		if (ctx->efd_ctx)eventfd_ctx_put(ctx->efd_ctx);
		free_inst_id(ctx->inst_id);
		kfree(ctx);
		return ret;
	}

	//// init video device
	ret = avs_video_device_init(ctx);
	if (ret) {
		v4l2_ctrl_handler_free(hdl);
		v4l2_device_unregister(&ctx->v4l2_dev);
		if (ctx->efd_ctx)eventfd_ctx_put(ctx->efd_ctx);
		free_inst_id(ctx->inst_id);
		kfree(ctx);
		return ret;
	}

	//// success
	*p_ctx = ctx;
	avs_camera_addref(ctx);

	printk("-- avs_camera_create OK\n");
	return 0;
}

void avs_camera_modify_param(
	struct avs_camera_devctx* cd, 
	struct ioctl_camera_create_t* ct)
{
	m_lock(cd);
	if (ct->fps > 0 && ct->fps <= 1000) {
		cd->sleep_msec = 1000 / ct->fps; ///
	}
	if (ct->name[0]) {
		memcpy(cd->name, ct->name, sizeof(cd->name)); ///
	}
	if (ct->event_fd > 0) {
		struct eventfd_ctx* efd = eventfd_ctx_fdget(ct->event_fd);
		if (efd) {
			if (cd->efd_ctx) eventfd_ctx_put(cd->efd_ctx);
			cd->efd_ctx = efd;
		}
	}
	///设置分辨率，
	if (ct->framesizes_count > 0 ) {
		memcpy(cd->frame_sizes, ct + 1, ct->framesizes_count * sizeof(struct ioctl_framesize_t));
		cd->frame_sizes_count = ct->framesizes_count;
	}
	m_unlock(cd);
}
int avs_camera_create(
	struct ioctl_camera_create_t* ct,
	struct avs_camera_devctx** p_ctx)
{
	struct avs_camera_devctx* ctx = NULL;	
	////
	sp_lock(avs);
	if ( !list_empty(&avs->lostsrc_cameras) ) {
		ctx = list_entry(avs->lostsrc_cameras.next, struct avs_camera_devctx, lostsrc_list);

		list_del_init(&ctx->lostsrc_list); //移除，并且清空
		////
		++ctx->user_count; //增加 count
	}
	sp_unlock(avs);

	if (ctx) {
		avs_camera_modify_param(ctx, ct);
		*p_ctx = ctx;
		evt_signal_withlock(ctx);// 通知参数变化

		printk("### fetch camera device from lost source queue.\n");
		return 0;
	}

	///////
	return avs_camera_create_real(ct, p_ctx);
}

/////
static void avs_camera_destroy_real(struct avs_camera_devctx* ctx)
{
	if (!ctx)return;
	/////
	printk("-- avs_camera_destroy\n");

	avs_camera_addref(ctx); ////增量

	/////
	sp_lock(ctx);
	ctx->is_destroy = true; ////
	sp_unlock(ctx);
	
	///////
	avs_camera_queue_cleanup(ctx);

	//////
	v4l2_device_unregister(&ctx->v4l2_dev);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);

	video_unregister_device(&ctx->dev);

	free_inst_id(ctx->inst_id);
	//////
	avs_camera_release(ctx); ////

	printk("--- avs_camera_destroy complete\n");
}

void avs_camera_destroy(struct avs_camera_devctx* ctx)
{
	bool is_lost = false;
	sp_lock(avs);
	--ctx->user_count;
	if (ctx->user_count > 0) {
		is_lost = true;
		////
		list_add_tail(&ctx->lostsrc_list, &avs->lostsrc_cameras); ///添加到数据源丢失队列

		avs_camera_addref(ctx); ////增量
		/////
	}
	else {
		is_lost = false;
		list_del_init(&ctx->lostsrc_list); //移除，并且清空
	}
	sp_unlock(avs);

	///////
	if (is_lost) {
		///
		printk("### some program had open camera, close video device after program closed.\n");

		///填充LOGO
		m_lock(ctx);
		if (ctx->video_image_buffer) {
			gen_yuy2_logo_picture(ctx, ctx->video_width, ctx->video_height, ctx->video_image_buffer);
		}
		m_unlock(ctx);

		avs_camera_release(ctx); ////
		/////
	}
	else {
		////
		avs_camera_destroy_real(ctx);
	}
	
}

/////
int avs_camera_alloc(struct avs_camera_devctx* ctx)
{
	int ret;
	sp_lock(avs);
	if ( list_empty(&ctx->lostsrc_list) ) { ///丢失数据源之后加入到队列，此处不为空; 否则为空
		++ctx->user_count; ////
		ret = 0;
	}
	else {
		printk("** Camera Source Had Closed, Not Open Again.\n");
		ret = -ENODEV;
	}
	sp_unlock(avs);

	/////
	return ret;
}

void avs_camera_free(struct avs_camera_devctx* ctx)
{
	bool is_free = false;
	sp_lock(avs);
	if (--ctx->user_count == 0) {
		is_free = true;
		list_del_init(&ctx->lostsrc_list); //移除，并且清空
	}
	sp_unlock(avs);

	////
	if (is_free) {
		printk("### real call [avs_camera_destroy] when /dev/videoXX had released.\n");
		avs_camera_destroy_real(ctx);
	}
}

//////////////////// 生成 LOGO图像
//////////////////// RGB -> YUV 从网络查询的算法
static void rgb24_yuy2(void* rgb, void* yuy2, int width, int height)
{
	int R1, G1, B1, R2, G2, B2, Y1, U1, Y2, V1;
	int i;
	unsigned char* pRGBData = (unsigned char *)rgb;
	unsigned char* pYUVData = (unsigned char *)yuy2;

	for ( i = 0; i<height; ++i)
	{
		int j;
		for ( j = 0; j<width / 2; ++j)
		{
			B1 = *(pRGBData + (height - i - 1)*width * 3 + j * 6);
			G1 = *(pRGBData + (height - i - 1)*width * 3 + j * 6 + 1);
			R1 = *(pRGBData + (height - i - 1)*width * 3 + j * 6 + 2);
			B2 = *(pRGBData + (height - i - 1)*width * 3 + j * 6 + 3);
			G2 = *(pRGBData + (height - i - 1)*width * 3 + j * 6 + 4);
			R2 = *(pRGBData + (height - i - 1)*width * 3 + j * 6 + 5);
			
			Y1 = ((66 * R1 + 129 * G1 + 25 * B1 + 128) >> 8) + 16; 
			U1 = (((-38 * R1 - 74 * G1 + 112 * B1 + 128) >> 8) + ((-38 * R2 - 74 * G2 + 112 * B2 + 128) >> 8)) / 2 + 128;
			Y2 = ((66 * R2 + 129 * G2 + 25 * B2 + 128) >> 8) + 16;
			V1 = (((112 * R1 - 94 * G1 - 18 * B1 + 128) >> 8) + ((112 * R2 - 94 * G2 - 18 * B2 + 128) >> 8)) / 2 + 128;

			*(pYUVData + i*width * 2 + j * 4) =     max(min( Y1, 255), 0);
			*(pYUVData + i*width * 2 + j * 4 + 1) = max(min( U1, 255), 0);
			*(pYUVData + i*width * 2 + j * 4 + 2) = max(min( Y2, 255), 0);
			*(pYUVData + i*width * 2 + j * 4 + 3) = max(min( V1, 255), 0);
		}
	}
}
//简单的插值算法缩放,从网上复制
static unsigned char *rgb24_scale(const unsigned char *src, 
	int srcWidth, int srcHeight, unsigned char* dst, int dstWidth, int dstHeight)
{
	unsigned char *buf = dst;

//	float horFactor0 = (float)srcWidth / dstWidth;  //水平缩放因子
//	float verFactor0 = (float)srcHeight / dstHeight; //垂直缩放因子
	///因为驱动中编译浮点运行，有点麻烦，所以干脆直接转成整数
#define SCALE_SIZE    1000
	int horFactor0 = srcWidth * SCALE_SIZE / dstWidth;     //水平缩放因子
	int verFactor0 = srcHeight * SCALE_SIZE / dstHeight;   //垂直缩放因子

	int x0, y0, i;
	for (i = 0; i < dstHeight; i++)
	{
		x0 = i * verFactor0 / SCALE_SIZE; int j;
		for (j = 0; j < dstWidth; j++)
		{
			y0 = j * horFactor0 / SCALE_SIZE;

			int srcOffset = (x0 * srcWidth + y0) * 3; // RGB 
			int dstOffset = (i * dstWidth + j) * 3;   //RGB

			buf[dstOffset + 0] = src[srcOffset + 0]; // B
			buf[dstOffset + 1] = src[srcOffset + 1]; // G
			buf[dstOffset + 2] = src[srcOffset + 2]; // R
		}
	}
	//////
	return buf;
}
void gen_yuy2_logo_picture(struct avs_camera_devctx* video, int w, int h, void* yuy2_buf)
{
	if (!video->logo_image_buffer)return;
	/////
	void* mem = NULL;
	unsigned char* rgb24 = NULL;
	if (w == video->logo_image_width && h == video->logo_image_height) {
		rgb24 = (unsigned char*)video->logo_image_buffer;
	}
	else {
		rgb24 = (unsigned char*)vmalloc( w*h * 3 );
		mem = rgb24;
		if (!mem)return;

		rgb24_scale((unsigned char*)video->logo_image_buffer, video->logo_image_width, video->logo_image_height, rgb24, w, h);
	}

	unsigned char* yuy2 = (unsigned char*)yuy2_buf;
	rgb24_yuy2(rgb24, yuy2, w, h);

	if (mem)vfree(mem);
}

