/// by fanxiushu 2024-11-16
#pragma once

////// 定义与应用层通讯的接口

#define CDO_NAME    "avs_virt_userio"   /// 用户接口名字

#define IOCTL_MAGIC                         'V'

#define IOCTL_CHECK_DRIVER_INSTALLED        _IO(IOCTL_MAGIC, 30)  /// 检测驱动安装情况，比如是否实现了 虚拟声卡，麦克风

#define IOCTL_DATA_TRANSFER                 _IO(IOCTL_MAGIC, 1)    /// 传输数据，对应 ioctl_avtrans_header_t 头

#define IOCTL_CREATE_CAMERA                 _IO(IOCTL_MAGIC, 10)   /// 创建虚拟摄像头，对应 ioctl_camera_create_t
#define IOCTL_GET_CAMERA_INFO               _IO(IOCTL_MAGIC, 11)   /// 获取虚拟摄像头信息
#define IOCTL_SET_CAMERA_LOGO               _IO(IOCTL_MAGIC, 12)   /// 设置摄像头 image logo

////分辨率最大数组，也就是最多只能生成20种分辨率
#define  MAX_FRAMESIZES_COUNT             20   ///

////////
#pragma pack(1)

////如果创建成功之后再次调用 IOCTL_CREATE_CAMERA， 则会尝试修改对应参数
struct ioctl_camera_create_t
{
	char        name[32 + 1];//摄像头名字

	int         fps; // FPS 帧率

	int         event_fd;  ///通知事件，用于驱动中，width，height，start/stop stream等变化之后通知应用层,由应用层 eventfd 创建提供
	/////
	int         framesizes_count;   //// 如果 framesizes_count > 0，后面跟 ioctl_framesize_t 数组
									//// 表示创建多少分辨率
};

///摄像头分辨率
struct ioctl_framesize_t
{
	int        width;
	int        height;
};

//// 对应 IOCTL_GET_CAMERA_INFO 命令，缓存大小应该设置 为
//// MAX_FRAMESIZES_COUNT*sizeof(ioctl_framesize_t) + sizeof(ioctl_camera_information_t)
struct ioctl_camera_information_t 
{
	int           video_width;
	int           video_height;
	int           frame_fps;     //
	////
	unsigned int  stream_start;  // 0 停止， >0 start

	////
	int           framesizes_count;   //// 返回实际分辨率的个数;
	                                  //// framesizes_count > 0，后面跟 ioctl_framesize_t 数组
};

////传输头
struct ioctl_avtrans_header_t
{
	int           type;            /// 传输类型， 1 摄像头视频传输，
	////
	union {
		struct {///因为驱动采用固定的YUY2格式，所以这里只有width和height
			int      width;
			int      height;
			int      res;         ///
			int      data_length; //数据长度，除开ioctl_avtrans_header_t
		}camera;
		/////
	};
};

#pragma pack()

