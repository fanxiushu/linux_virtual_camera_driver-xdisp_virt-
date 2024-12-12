/// by fanxiushu 2024-11-16, V4L2 Virtual Camera Driver
#pragma once
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/font.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/eventfd.h>
#include <linux/vmalloc.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-common.h>

///�汾�Ų�һ��׼ȷ,������Լ��汾���������
#define VB2_STRUCT_VERSION KERNEL_VERSION(4,4,0)

/////////////////���°汾�Ų�һ��׼ȷ,������Լ��汾���������
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5,10,160)

#define CLASS_CREATE(str) class_create(THIS_MODULE, str)
#define EVENTFD_SIGNAL(evt)  eventfd_signal(evt, 1)

#else
#define CLASS_CREATE(str) class_create(str)
#define EVENTFD_SIGNAL(evt)  eventfd_signal(evt)
#endif

/////////
#include "ioctl.h"

#define DRIVER_AUTHOR      "Fanxiushu "
#define DRIVER_DESC        "Fanxiushu Virtual Audio/Video Stream Driver"
#define AVS_VERSION        "1.0.1"

#define MAX_INSTANCE_COUNT 1000
struct avs_virt_t
{
	////// user cdev
	dev_t               cdo_devid;
	struct  cdev        cdo_dev;
	struct  class*      cdo_cls;

	////////
	unsigned long              splock_flags;//
	spinlock_t                 spinlock;
	struct list_head           lostsrc_cameras; //�Ѿ�ʧȥ����Դ���豸
	uint8_t                    inst_ids[MAX_INSTANCE_COUNT];

};
extern struct avs_virt_t __gbl_avs;
#define avs  (&__gbl_avs)

////// 

#define PIX_FMT    V4L2_PIX_FMT_YUYV
#define BIT_COUNT  16

//////////////// v4l2 ����ͷ���ݽṹ
struct avs_camera_devctx
{
	////
	struct video_device	       dev;
	struct v4l2_device 	       v4l2_dev;
	struct v4l2_ctrl_handler   ctrl_handler;
	struct vb2_queue           vb2_q;  //// 
	atomic_t                   kref_count;
	unsigned int               user_count; ///����ʹ�õ�user������Ҳ�������ڴ� /dev/videoXX ���ļ�����
	bool                       is_destroy; ///�Ƿ��Ѿ�����
	int                        inst_id;    //// ûɶ�ã����Ǽ�¼��ǰ�豸��ID��
	u32                        sequence;   //// ���кţ�û�е��������кţ� kernel �߰汾 cheese��������

	struct list_head           lostsrc_list; ///������Դ��ʧ�����ص�ȫ�ֶ�����

	///// lock
	unsigned long              splock_flags;//
	spinlock_t                 spinlock;   ////����������������
	struct mutex               v4l2_mutex; /// ���� ���� V4L2 ͬ����
	struct mutex               mutex;      ////����ͼ�����ݵ�ͬ����, ��Ϊ���ǵ���v4l2���ʹ��ͬһ�������ܷ��������ȣ����Ըɴ�����ʹ�á�
	
	////
	char                       name[32 + 1]; ////
	struct  eventfd_ctx*       efd_ctx;      ///����֪ͨ

	int                        frame_sizes_count;   //// �ֱ��ʸ����������� MAX_FRAMESIZES_COUNT
	struct ioctl_framesize_t   frame_sizes[MAX_FRAMESIZES_COUNT]; ///�ֱ���

	int                        sleep_msec; //// 1000/ fps;
	int                        video_width;
	int                        video_height;
	char*                      video_image_buffer;  // ���� vmalloc �������ڴ棬ȷ���ܷ������ڴ�
	// ���������,����������Щ����������Ҫ�� mutex�½���

	unsigned int               stream_start_count;  /// 

	////���ڴ洢��Ӧ�ò��ȡ��LOGOͼƬ��RGB24��ʽ
	char*                      logo_image_buffer;  //���� vmalloc �������ڴ棬ȷ���ܷ������ڴ�
	int                        logo_image_width;
	int                        logo_image_height;
	////���������,����������Щ����������Ҫ�� mutex�½���

	//////
	struct task_struct*        kthread;
	wait_queue_head_t          wait_q;

	struct list_head           video_buffer_head; /// ���� �״̬ һ֡����, -> avs_video_buffer

	/////
};
////avs_camera_devctx ��ߵ���������mutex��
#define sp_lock(v)    spin_lock_irqsave(&v->spinlock, v->splock_flags);
#define sp_unlock(v)  spin_unlock_irqrestore(&v->spinlock, v->splock_flags);

#define m_lock(v)     mutex_lock(&v->mutex);
#define m_unlock(v)   mutex_unlock(&v->mutex);

#define evt_signal_withlock(ctx) \
           m_lock(ctx); if (ctx->efd_ctx) EVENTFD_SIGNAL(ctx->efd_ctx); m_unlock(ctx);

///////////
struct avs_video_buffer
{
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
	struct vb2_v4l2_buffer  buffer;  /// ���������ǰ��
#else
	struct vb2_buffer       buffer;  /// ���������ǰ��
#endif

	struct list_head        list;    /// -> avs_camera_devctx.video_buffer_head

};

/// inline function
static inline void avs_camera_addref(struct avs_camera_devctx* ctx) 
{
	atomic_inc(&ctx->kref_count);
}
static inline void avs_camera_release(struct avs_camera_devctx* ctx) 
{
	if (atomic_dec_and_test(&ctx->kref_count)) {
		////
		if (ctx->video_image_buffer) {
			vfree(ctx->video_image_buffer);
		}
		if (ctx->logo_image_buffer) {
			vfree(ctx->logo_image_buffer);
		}
		if (ctx->efd_ctx)eventfd_ctx_put(ctx->efd_ctx);

		printk("--- avs_camera_release: \n" );
		kfree(ctx);
	}
}
static inline void avs_camera_queue_cleanup(
	struct avs_camera_devctx* ctx)
{
	////ֹͣthread
	struct task_struct* kthr = NULL;
	sp_lock(ctx);
	kthr = ctx->kthread;  //
	ctx->kthread = NULL;
	sp_unlock(ctx);
	if (kthr) {
		kthread_stop(kthr);
		/////
	}

	/////release all buffer
	sp_lock(ctx);
	while (!list_empty(&ctx->video_buffer_head)) {
		struct avs_video_buffer *buf;
		buf = list_entry(ctx->video_buffer_head.next, struct avs_video_buffer, list);
		list_del(&buf->list);
#if LINUX_VERSION_CODE >= VB2_STRUCT_VERSION
		vb2_buffer_done(&buf->buffer.vb2_buf, VB2_BUF_STATE_ERROR);
#else
		vb2_buffer_done(&buf->buffer, VB2_BUF_STATE_ERROR);
#endif
//		printk(KERN_ALERT"[%p/%d] done\n", buf, buf->buffer.v4l2_buf.index);
	}
	sp_unlock(ctx);

}

//// function
int  cdo_init(void);
void cdo_deinit(void);

int avs_vb2_queue_init(struct avs_camera_devctx* ctx);
int avs_video_device_init(struct avs_camera_devctx* ctx);

void avs_camera_modify_param(
	struct avs_camera_devctx* cd,
	struct ioctl_camera_create_t* ct);

int avs_camera_create(
	struct ioctl_camera_create_t* ct,
	struct avs_camera_devctx** p_ctx);

void avs_camera_destroy(struct avs_camera_devctx* ctx);

int avs_camera_alloc(struct avs_camera_devctx* ctx);
void avs_camera_free(struct avs_camera_devctx* ctx);

void gen_yuy2_logo_picture(struct avs_camera_devctx* video, int w, int h, void* yuy2_buf);

