#include "QFC.h"

#include "ringbuf_dynamic.c"

ringBuffer_t *rBuffer[MAX_CAMS];

int main(int argc, char **argv[])
{

    // Start a test with 1 Camera (this would be normally executed from the main executable)

    int8_t result = 0;
    int32_t frameSize = 0;
    uint8_t *framePointer;

    result = startStreaming(0, "rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mp4", 1920, 1080);

    // Do something else for 2 seconds to wait and see if connected...
    sleep(2);

    if (!isConnected(0))
    {
        fprintf(stderr, "Main: Cam #0 is not connected!\n");
        return 0;
    }

    frameSize = getFrameSize(0);

    fprintf(stderr, "Main: Cam 0 got a framesize of %d\n", frameSize);

   
    while (isCapturing(0))
    {
        framePointer = getFrame(0);
        
        if (!framePointer) {
            // no more frames to process... loop until we do.
            continue;
        }

        frameSize = getFrameSize(0);

        // Use frame (yuv420p) data to contruct something useful...
        printf("Main: Cam 0 -> We have a usable frame (%d)! size is %d bytes. Location is %p\n", rBuffer[0]->count, frameSize, framePointer);
        stopStreaming(0);
        break;
    }
    
    printf("Main: Exiting main program...\n");
    getchar();
    return 0;
}

/**
 * WITHIN a thread accessing global variable "cams" , but only using a unique id.
 *
 */
DLL_PUBLIC int8_t startStreaming(uint8_t camId, const char *rtspAddress, uint32_t width, uint32_t height)
{

    if (cams[camId].isConnected)
    {
        return -1;
    }

    cams[camId].camId = camId;
    cams[camId].requestedHeight = height;
    cams[camId].requestedWidth = width;

    strncpy(cams[camId].rtspAddress, rtspAddress, sizeof(cams[camId].rtspAddress) - 1);

    launchThread(&cams[camId]);
}

DLL_PUBLIC uint8_t stopStreaming(uint8_t camId)
{

    if (isValidCamId(camId))
    {
        cams[camId].isEOS = 1;
    }
    else
    {
        return 0;
    }
    return 1;
}

DLL_LOCAL void launchThread(void *args)
{

#if defined _WIN32

#else
    pthread_t thread_Id;

    uint32_t result = 0;
    result = pthread_create(&thread_Id, NULL, &ffmpegStartStreaming, args);

#endif
}

DLL_PUBLIC uint8_t isCapturing(uint8_t camId)
{
    if (isValidCamId(camId))
        return cams[camId].isCapturing;
}

DLL_PUBLIC uint8_t isConnected(uint8_t camId)
{
    if (isValidCamId(camId))
        return cams[camId].isConnected;
}

DLL_PUBLIC size_t getFrameSize(uint8_t camId)
{

    if (!rBuffer[camId])
    {
        return 0;
    }

    return rBuffer[camId]->size;
}

DLL_PUBLIC uint8_t* getFrame(uint8_t camId)
{

    uint8_t *result;

    if (!isValidCamId(camId))
        return 0;

    result = rb_pop(rBuffer[camId]);

    return result;
}

DLL_LOCAL uint8_t isValidCamId(uint8_t camId)
{
    if (camId >= 0 && camId <= MAX_CAMS)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

DLL_LOCAL void *ffmpegStartStreaming(void *args)
{

    const AVCodec *codec = NULL;
    AVCodecContext *codecContext = NULL;
    AVFormatContext *formatContext = NULL;
    AVPacket *packet = NULL;
    int32_t videoStreamId = 0;
    AVFrame *frame = NULL;
    AVFrame *convertFrame = NULL;
    AVFrame *toWriteFrame = NULL;
    int32_t result;
    uint32_t imageBufferSize = 0;
    struct SwsContext *resize = NULL;
    uint8_t mustResize = 0;
    uint32_t convertFrameSize = 0;
    uint8_t *convertFrameBuffer = NULL;
    uint8_t *finalFrameBuffer = NULL;

    FILE *fp;

    cam_t *cam = (cam_t *)args;

    formatContext = avformat_alloc_context();
    // Start ffmpeg process ...

    avformat_network_init();

    if (avformat_open_input(&formatContext, cam->rtspAddress, NULL, NULL) != 0)
    {
        fprintf(stderr, "%s FATAL: Cam #%d cannot connect to %s\n", __FILE__, cam->camId, cam->rtspAddress);
        goto cleanup;
    }

    cam->isConnected = 1;

    if (avformat_find_stream_info(formatContext, NULL) < 0)
    {
        goto cleanup;
    }

    videoStreamId = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    if (videoStreamId < 0)
    {
        fprintf(stderr, "%s FATAL: Cam #%d -> Could not find a video stream!\n", __FILE__, cam->camId);
        goto cleanup;
    }

    // Try to find a h265 codec?
    codec = avcodec_find_decoder(formatContext->streams[videoStreamId]->codecpar->codec_id);
    if (!codec)
    {
        // This is pretty sad, cannot recover...
        fprintf(stderr, "%s FATAL: Cam #%d -> Cannot find a decoder for this stream, exiting\n", __FILE__, cam->camId);
        goto cleanup;
    }

    codecContext = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamId]->codecpar);

    // Open codec ...
    if (avcodec_open2(codecContext, codec, NULL) < 0)
    {
        fprintf(stderr, "%s FATAL: Cam #%d -> Error opening with avcodec_open2\n", __FILE__, cam->camId);
        goto cleanup;
    }

    frame = av_frame_alloc();
    convertFrame = av_frame_alloc();
    packet = av_packet_alloc();

    if (!frame || !packet)
    {
        fprintf(stderr, "%s FATAL: Cannot allocate frame or packet\n", __FILE__);
        goto cleanup;
    }

    if (cam->requestedHeight && cam->requestedHeight != frame->height)
    {
        mustResize = 1;
    }

    if (cam->requestedWidth && cam->requestedWidth != frame->width)
    {
        mustResize = 1;
    }

    cam->isEOS = 0;

    while (!cam->isConnected || !cam->isEOS)
    {
        if ((result = av_read_frame(formatContext, packet)) < 0)
        {
            break;
        }

        if (packet->stream_index != videoStreamId)
        {
            continue;
        }

        cam->isCapturing = 1;

        result = avcodec_send_packet(codecContext, packet);

        if (result < 0)
        {
            fprintf(stderr, "%s FATAL: Cam #%d decoder error (%d)\n", __FILE__, cam->camId, result);
            break;
        }

        while (result >= 0 && !cam->isEOS)
        {

            result = avcodec_receive_frame(codecContext, frame);

            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            {
                // ignore and continue
                break;
            }

            if (result < 0)
            {
                fprintf(stderr, "%s FATAL: Cam %d -> Error while fetching frame\n", __FILE__, cam->camId);
                goto cleanup;
            }

            fprintf(stderr, "%s INFO: Cam #%d -> Frame %d (%dx%d), type = %c , size = %d bytes, format = %d (%s), pts %ld keyFrame %d\n",
                    __FILE__,
                    cam->camId,
                    codecContext->frame_num,
                    frame->width,
                    frame->height,
                    av_get_picture_type_char(frame->pict_type),
                    frame->pkt_size,
                    frame->format,
                    av_get_pix_fmt_name(frame->format),
                    frame->pts,
                    frame->key_frame);

            /**
             * Check if in YUV420P, if not , convert or check if we must resize
             */
            if (codecContext->pix_fmt != AV_PIX_FMT_YUV420P || mustResize)
            {

                if (resize == NULL)
                {
                    resize = sws_getContext(
                        codecContext->width,
                        codecContext->height,
                        frame->format,
                        cam->requestedWidth ? cam->requestedWidth : codecContext->width,
                        cam->requestedHeight ? cam->requestedHeight : codecContext->height,
                        AV_PIX_FMT_YUV420P,
                        SWS_BICUBIC, NULL, NULL, NULL

                    );

                    convertFrameSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                                                                cam->requestedWidth ? cam->requestedWidth : frame->width,
                                                                cam->requestedHeight ? cam->requestedHeight : frame->height,
                                                                1);

                    convertFrameBuffer = (uint8_t *)av_malloc(convertFrameSize * sizeof(uint8_t));
                    av_image_fill_arrays(
                        convertFrame->data,
                        convertFrame->linesize,
                        convertFrameBuffer,
                        AV_PIX_FMT_YUV420P,
                        cam->requestedWidth ? cam->requestedWidth : frame->width,
                        cam->requestedHeight ? cam->requestedHeight : frame->height,
                        1);
                }

                convertFrame->width = cam->requestedWidth ? cam->requestedWidth : frame->width;
                convertFrame->height = cam->requestedHeight ? cam->requestedHeight : frame->height;

                // Actual scaling
                sws_scale(resize, (const uint8_t *const *)(frame->data), (frame->linesize), 0, frame->height, convertFrame->data, convertFrame->linesize);

                toWriteFrame = convertFrame;
            }
            else
            {
                toWriteFrame = frame;
            }

            if (toWriteFrame->format == -1)
            {
                toWriteFrame->format = AV_PIX_FMT_YUV420P;
            }

            imageBufferSize = av_image_get_buffer_size(toWriteFrame->format, toWriteFrame->width, toWriteFrame->height, 1);

            fprintf(stderr, "%s INFO: OUTPUT Cam #%d -> Frame format %d (%s), %dx%d, %d bytes ready to be consumed\n",
                    __FILE__,
                    cam->camId,
                    toWriteFrame->format,
                    av_get_pix_fmt_name(toWriteFrame->format),
                    toWriteFrame->width,
                    toWriteFrame->height,
                    imageBufferSize);

            finalFrameBuffer = av_malloc(imageBufferSize);
            av_image_copy_to_buffer(finalFrameBuffer, imageBufferSize,
                                    (const uint8_t *const *)toWriteFrame->data,
                                    (const int *)toWriteFrame->linesize, toWriteFrame->format,
                                    toWriteFrame->width, toWriteFrame->height, 1);

            if (!rBuffer[cam->camId])
            {
                rBuffer[cam->camId] = malloc(sizeof(ringBuffer_t));
                rb_init(rBuffer[cam->camId], MAX_BUFFERED_FRAMES, imageBufferSize);
            }

            // Lock?!??!
            rb_push(rBuffer[cam->camId], finalFrameBuffer);

            
        }
        av_packet_unref(packet);
    }

cleanup:
    fprintf(stderr, "%s Exiting thread (cleanup) of cam #%d\n", __FILE__, cam->camId);
    cam->isCapturing = 0;
    // Cleanup
    avformat_network_deinit();
    cam->isConnected = 0;
    avformat_close_input(&formatContext);
    avformat_free_context(formatContext);

    if (codec)
    {
        avcodec_close(codecContext);
        avcodec_free_context(&codecContext);
    }

    if (packet)
    {
        av_packet_free(&packet);
    }

    if (frame)
    {
        av_frame_free(&frame);
    }

    if (convertFrame)
    {
        av_frame_free(&convertFrame);
    }
    if (convertFrameBuffer)
    {
        av_free(convertFrameBuffer);
    }
    if (resize)
    {
        sws_freeContext(resize);
    }
    if (finalFrameBuffer)
    {
        av_free(finalFrameBuffer);
    }
    if (rBuffer[cam->camId])
    {
        rb_free(rBuffer[cam->camId]);
        free(rBuffer[cam->camId]);
    }

    pthread_exit(0);
}
