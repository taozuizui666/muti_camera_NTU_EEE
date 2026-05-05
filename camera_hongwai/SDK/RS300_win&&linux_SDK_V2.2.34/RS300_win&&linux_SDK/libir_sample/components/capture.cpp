#include "capture.h"

void process_cb(double process_num)
{
  printf("process_cb:%.2f\n",process_num);
}

bool yuyv_2_yuv422p(uint8_t* data, int width, int height, uint8_t* out_data)
{
    if (data == NULL)
    {
        return false;
    }
    int pix_size = width * height;
    int i = 0;  //data pixel index
    int i_y = 0, i_u = 0, i_v = 0;    //out_data yuv index
    int y1, y2, u1, v2;
    while (i < pix_size)
    {
        y1 = data[2 * i];
        y2 = data[2 * (i + 1)];
        u1 = data[2 * i + 1];
        v2 = data[2 * (i + 1) + 1];

        out_data[i_y] = data[2 * i];                //y1
        out_data[i_u + pix_size] = u1;              //u1
        out_data[i_y + 1] = data[2 * (i+1)];        //y2
        out_data[i_v + pix_size * 3 / 2] = v2;      //v2

        i += 2;
        i_y += 2;
        i_u++;
        i_v++;
    }
    return true;
}

int yuyv_2_jpg(uint8_t* data, int width, int height, const char* output_filename)
{
    if (data == NULL)
    {
        return -1;
    }

    AVFormatContext* pFormatCtx;
    AVOutputFormat*  pOutFmt;
    AVStream* pStream;
    AVCodecContext* pCodecCtx;
    AVCodec* pCodec;
    AVFrame* pFrame;
    AVPacket pkt;

    uint8_t* picture_buf;
    int y_size = 0;
    int size = 0;
    int got_picture = 0;
    int ret = 0;

    av_register_all();

    pFormatCtx = avformat_alloc_context();
    pOutFmt = av_guess_format("mjpeg", NULL, NULL); //set the format of the output file
    pFormatCtx->oformat = pOutFmt;

    if (avio_open(&pFormatCtx->pb, output_filename, AVIO_FLAG_READ_WRITE) < 0) //create and init an AVIOContext
    {
        printf("Could not open output file!\n");
        return -1;
    }

    pStream = avformat_new_stream(pFormatCtx, 0);
    if (NULL == pStream)
        return -1;
    //set related infomation
    pCodecCtx = avcodec_alloc_context3(NULL);
    if (pCodecCtx == NULL)
    {
        printf("Could not allocate AVCodecContext\n");
        return -1;
    }
    avcodec_parameters_to_context(pCodecCtx, pStream->codecpar);
    pCodecCtx->codec_id = pOutFmt->video_codec;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ422P;
    pCodecCtx->width = width;
    pCodecCtx->height = height;
    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = 25;

    //av_dump_format(pFormatCtx, 0, output_filename, 1);

    pCodec = avcodec_find_encoder(pCodecCtx->codec_id); //find codec
    if (NULL == pCodec)
    {
        printf("can not find codec!\n");
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        printf("con not open codec!\n");
        return -1;
    }

    pFrame = av_frame_alloc();
    size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 1);//get image's size
    picture_buf = (uint8_t*)av_malloc(size);
    if (NULL == picture_buf)
    {
        printf("malloc picture buf error!\n");
        return -1;
    }
    av_image_fill_arrays(pFrame->data, pFrame->linesize, picture_buf, pCodecCtx->pix_fmt, \
                        pCodecCtx->width, pCodecCtx->height, 1);

    avformat_write_header(pFormatCtx, NULL);

    y_size = pCodecCtx->width * pCodecCtx->height;
    av_new_packet(&pkt, y_size*2);


    pFrame->width = pCodecCtx->width;
    pFrame->height =  pCodecCtx->height;
    pFrame->format = pCodecCtx->pix_fmt;

    //convert YUY2 data to YUV422P
    yuyv_2_yuv422p(data, width, height, picture_buf);
    pFrame->data[0] = picture_buf;
    pFrame->data[1] = picture_buf + y_size;
    pFrame->data[2] = picture_buf + y_size*3/2;

    ret = avcodec_send_frame(pCodecCtx, pFrame);
    if (ret < 0)
    {
        printf("avcodec_send_frame error!\n");
        return -1;
    }

    av_init_packet(&pkt);
    got_picture = avcodec_receive_packet(pCodecCtx, &pkt);//codec the data
    if (ret < 0)
    {
        printf("encodec yuv data error!\n");
        return -1;
    }
    pkt.stream_index = pStream->index;
    //avoid warning "Encoder did not produce proper pts, making some up"
    pkt.pts = av_rescale_q(pkt.pts, pStream->time_base, pStream->time_base);
    pkt.dts = av_rescale_q(pkt.dts, pStream->time_base, pStream->time_base);

    av_write_frame(pFormatCtx, &pkt); //write data

    av_write_trailer(pFormatCtx);

    av_packet_unref(&pkt);
    av_free(picture_buf);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avcodec_free_context(&pCodecCtx);
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);

    return 0;
}

void* capture_function(void* threadarg)
{
    printf("capture_function start\n");
    StreamFrameInfo_t* stream_frame_info;
    stream_frame_info = (StreamFrameInfo_t*)threadarg;
    uint8_t* yuv444_image_frame = NULL;
    uint8_t* rgb_image_frame = NULL;
	if (stream_frame_info == NULL)
	{
		return NULL;
	}

    int fps = stream_frame_info->camera_param.fps;
    int i=0;
    char name[100] = {0};
    while (is_streaming)
    {
        if (i > 50*fps)   //limit the max capture time
        {
            break;
        }

#if defined(_WIN32)
        WaitForSingleObject(cap_sem, INFINITE);	//waitting for image singnal
#elif defined(linux) || defined(unix)
        sem_wait(&cap_sem);
#endif
        if ((i > 3 * fps)&&(i % fps == 0))   //stream on 3s later, capture image every second
        {
            timeb now;
            ftime(&now);
            printf("%lu.jpg\n", now.time * 1000 + now.millitm);
            sprintf(name, "%lu.jpg", now.time * 1000 + now.millitm);
            yuyv_2_jpg(stream_frame_info->image_frame, stream_frame_info->image_info.width, stream_frame_info->image_info.height, name);
        }
        i++;

#if defined(_WIN32)
        ReleaseSemaphore(cap_done_sem, 1, NULL);
#elif defined(linux) || defined(unix)
        sem_post(&cap_done_sem);
#endif
    }

    return NULL;
}