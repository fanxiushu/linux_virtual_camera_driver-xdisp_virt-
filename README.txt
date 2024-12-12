这个驱动是linux平台下实现虚拟摄像头的源码，
用于xdisp_virt程序Linux版本的的虚拟摄像头，

linux的虚拟摄像头驱动以源码提供，针对不同的linux内核版本，
可以自行编译，编译安装之后，
可与xdisp_virt配合，实现电脑端的虚拟摄像头功能。

编译方法：
进入linux系统，需要安装gcc等编译环境，具体可通过其他途径查询。
之后直接进入本驱动driver目录，make 即可编译，
成功之后，会生成 avstrm_virt.ko 驱动文件。

安装方法：
驱动需要加载其他模块，因此不能简单的insmod，需要通过modprobe安装。
大致步骤如下：
1,   cp ./avstrm_virt.ko /lib/modules/`-uname -r`/
2,   depmod -a
3,   modprobe avstrm_virt

已经尝试过的linux系统：
1， CentOS7，kernel3.10， 成功运行
2， Ubuntu24.10, kernel6.11，成功运行
3， REHL9.5  kernel5.14  ， 成功运行
4，其他kernel内核没尝试过，可自行去尝试，如果编译遇到不通过，可尝试修改驱动相关代码解决。