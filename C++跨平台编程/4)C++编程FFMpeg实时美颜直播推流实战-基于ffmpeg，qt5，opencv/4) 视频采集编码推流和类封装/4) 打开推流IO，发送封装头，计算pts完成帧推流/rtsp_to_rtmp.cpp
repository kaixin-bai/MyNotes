#include <opencv2/highgui.hpp>
#include <iostream>
#include <stdexcept>
extern "C"
{
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
using namespace cv;
using namespace std;

#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "opencv_world320.lib")

int main(int argc, char** argv)
{
	// 海康相机的rtsp url
	char *inUrl = "rtsp://admin:dejan1314520@192.168.25.101:554/onvif1";
	// nginx-rtmp 直播服务器rtmp推流URL
	char *outUrl = "rtmp://192.168.163.131/live";
	// 注册所有的编解码器
	avcodec_register_all();

	// 注册所有的封装器
	av_register_all();

	// 注册所有网络协议
	avformat_network_init();

	VideoCapture cam;
	Mat frame;
	namedWindow("video");

	// 像素格式转换上下文
	SwsContext *vsc = NULL;
	// 输出的数据结构
	AVFrame *yuv = NULL;

	// 编码器上下文
	AVCodecContext *vc = NULL;

	// rtmp flv 封装器
	AVFormatContext *ic = NULL;

	//cam.open(inUrl);
	cam.open(0);
	try {
		/////////////////////////////////////////////////
		/// 1 使用OpenCV打开rtsp相机
		if (!cam.isOpened())
		{
			throw exception("cam open failed!");
		}
		cout << "inUrl cam open success!" << endl;
		int inWidth = cam.get(CAP_PROP_FRAME_WIDTH);
		int inHeight = cam.get(CAP_PROP_FRAME_HEIGHT);
		//int fps = cam.get(CAP_PROP_FPS); // 系统摄像头会报错! [libx264 @ 04e57880] The encoder timebase is not set. error:Invalid argument
		int fps = 3; // 每秒传输帧数

		/// 2 初始化格式转换上下文
		vsc = sws_getCachedContext(vsc,
			inWidth, inHeight,  // 源宽、高
			AV_PIX_FMT_BGR24,   // 源像素格式
			inWidth, inHeight,  // 目标宽、高
			AV_PIX_FMT_YUV420P, // 目标像素格式
			SWS_BICUBIC, // 尺寸变化使用算法
			0, 0, 0
		);
		if (!vsc)
		{
			throw exception("sws_getCachedContext() failed!");
		}
		/// 3 输出的数据结构
		yuv = av_frame_alloc();
		yuv->format = AV_PIX_FMT_YUV420P;
		yuv->width = inWidth;
		yuv->height = inHeight;
		yuv->pts = 0;
		// 分配yuv空间
		int ret = av_frame_get_buffer(yuv, 32);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}

		/// 4 初始化编码上下文
		// a 找到编码器
		AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!codec)
		{
			throw exception("Can't find h264 encoder!");
		}
		
		// b 创建编码器上下文
		vc = avcodec_alloc_context3(codec);
		if (!vc)
		{
			throw exception("avcodec_alloc_context3() failed!");
		}
		// c 配置编码器参数
		vc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // 全局参数
		vc->codec_id = codec->id;
		vc->thread_count = 4;

		vc->bit_rate = 50 * 1024 * 8; // 压缩后每秒视频的bit位大小 50kB
		vc->width = inWidth;
		vc->height = inHeight;
		vc->time_base = { 1,fps }; // 时间基数 1/3秒
		vc->framerate = { fps,1 }; // 帧率

		// 画面组的大小, 多少帧一个关键帧
		vc->gop_size = 50; // 每到50一个关键帧 I帧
		vc->max_b_frames = 0; // 没有B帧 I帧后面全部是P帧
		vc->pix_fmt = AV_PIX_FMT_YUV420P;

		// d 打开编码器上下文
		ret = avcodec_open2(vc, 0, 0);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}
		cout << "avcodec_open2() success!" << endl;
		
		///5 封装器和视频流配置
		// a 创建输出封装器上下文
		ret = avformat_alloc_output_context2(&ic,0,"flv",outUrl);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}
		// b 添加视频流
		AVStream *vs = avformat_new_stream(ic, NULL);
		if (!vs)
		{
			throw exception("avformat_new_stream() failed!");
		}
		vs->codecpar->codec_tag = 0; // 指定编码格式
		// 从编码器复制参数
		avcodec_parameters_from_context(vs->codecpar, vc);
		av_dump_format(ic, 0, outUrl, 1);

		///6 打开rtmp的网络输出IO
		ret = avio_open(&ic->pb, outUrl,AVIO_FLAG_WRITE);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}
		// 写入封装头
		ret = avformat_write_header(ic, NULL);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}


		AVPacket pack;
		memset(&pack, 0, sizeof(pack));
		int vpts = 0;
		for (;;)
		{
			// 读取rtsp视频帧, 解码视频帧
			if (!cam.grab()) continue;
			// yuv转换为rgb
			if (!cam.retrieve(frame)) continue;
			imshow("vidoe", frame);
			waitKey(1);
			// rgb to yuv

			// 输入的数据结构
			uint8_t *indata[AV_NUM_DATA_POINTERS] = { 0 };
			// bgrbgr
			// plane indata[0] bbbb indata[1] gggg indata[2] rrrr
			indata[0] = frame.data;
			int insize[AV_NUM_DATA_POINTERS] = { 0 };
			// 一行(宽)数据的字节数
			insize[0] = frame.cols * frame.elemSize();

			int h = sws_scale(
				vsc, indata, insize, 0, frame.rows, // 源数据
				yuv->data, yuv->linesize
			);
			if (h <= 0) continue;
			//cout << h << " " << flush;

			/// h264编码
			yuv->pts = vpts;
			vpts++;
			ret = avcodec_send_frame(vc, yuv);
			if (ret != 0) continue;
			ret = avcodec_receive_packet(vc, &pack);
			if (ret != 0 || pack.size > 0)
			{
				//cout << "*" <<pack.size<< flush;
			}
			else
			{
				continue;
			}
			// 推流
			pack.pts = av_rescale_q(pack.pts, vc->time_base, vs->time_base);
			pack.dts = av_rescale_q(pack.dts, vc->time_base, vs->time_base);
			ret = av_interleaved_write_frame(ic, &pack);
			if (ret == 0)
			{
				cout << "#" << flush;
			}

		}
	}
	catch (exception &e)
	{
		if (cam.isOpened()) cam.release();
		if (vsc)
		{
			sws_freeContext(vsc);
			vsc = NULL;
		}
		if (vc)
		{
			avio_closep(&ic->pb);
			avcodec_free_context(&vc);
		}
		cout << "error:" << e.what() << endl;
	}

	getchar();
	return 0;
}