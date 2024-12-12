/// by fanxiushu 2024-11-16
#pragma once

////// ������Ӧ�ò�ͨѶ�Ľӿ�

#define CDO_NAME    "avs_virt_userio"   /// �û��ӿ�����

#define IOCTL_MAGIC                         'V'

#define IOCTL_CHECK_DRIVER_INSTALLED        _IO(IOCTL_MAGIC, 30)  /// ���������װ����������Ƿ�ʵ���� ������������˷�

#define IOCTL_DATA_TRANSFER                 _IO(IOCTL_MAGIC, 1)    /// �������ݣ���Ӧ ioctl_avtrans_header_t ͷ

#define IOCTL_CREATE_CAMERA                 _IO(IOCTL_MAGIC, 10)   /// ������������ͷ����Ӧ ioctl_camera_create_t
#define IOCTL_GET_CAMERA_INFO               _IO(IOCTL_MAGIC, 11)   /// ��ȡ��������ͷ��Ϣ
#define IOCTL_SET_CAMERA_LOGO               _IO(IOCTL_MAGIC, 12)   /// ��������ͷ image logo

////�ֱ���������飬Ҳ�������ֻ������20�ֱַ���
#define  MAX_FRAMESIZES_COUNT             20   ///

////////
#pragma pack(1)

////��������ɹ�֮���ٴε��� IOCTL_CREATE_CAMERA�� ��᳢���޸Ķ�Ӧ����
struct ioctl_camera_create_t
{
	char        name[32 + 1];//����ͷ����

	int         fps; // FPS ֡��

	int         event_fd;  ///֪ͨ�¼������������У�width��height��start/stop stream�ȱ仯֮��֪ͨӦ�ò�,��Ӧ�ò� eventfd �����ṩ
	/////
	int         framesizes_count;   //// ��� framesizes_count > 0������� ioctl_framesize_t ����
									//// ��ʾ�������ٷֱ���
};

///����ͷ�ֱ���
struct ioctl_framesize_t
{
	int        width;
	int        height;
};

//// ��Ӧ IOCTL_GET_CAMERA_INFO ��������СӦ������ Ϊ
//// MAX_FRAMESIZES_COUNT*sizeof(ioctl_framesize_t) + sizeof(ioctl_camera_information_t)
struct ioctl_camera_information_t 
{
	int           video_width;
	int           video_height;
	int           frame_fps;     //
	////
	unsigned int  stream_start;  // 0 ֹͣ�� >0 start

	////
	int           framesizes_count;   //// ����ʵ�ʷֱ��ʵĸ���;
	                                  //// framesizes_count > 0������� ioctl_framesize_t ����
};

////����ͷ
struct ioctl_avtrans_header_t
{
	int           type;            /// �������ͣ� 1 ����ͷ��Ƶ���䣬
	////
	union {
		struct {///��Ϊ�������ù̶���YUY2��ʽ����������ֻ��width��height
			int      width;
			int      height;
			int      res;         ///
			int      data_length; //���ݳ��ȣ�����ioctl_avtrans_header_t
		}camera;
		/////
	};
};

#pragma pack()

