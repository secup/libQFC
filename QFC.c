#include "QFC.h"

#include "ringbuf_dynamic.c"

ringBuffer_t *rBuffer[MAX_CAMS];

int main(int argc, char **argv[])
{

    // Start a test with Camera 1

    uint8_t result = 0;

    //result = startStreaming(0, "rtsp://zephyr.rtsp.stream/pattern?streamKey=2264d029cc1d77c0afb347e4470a8b03", 1920, 1080);
    result = startStreaming(0,"rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mp4", 1920, 1080);

    getchar();
    return 0;
}

/**
 * WITHIN a thread accessing global variable "cams" , but only using a unique id.
 *
 */
int8_t DLL_PUBLIC startStreaming(uint8_t camId, const char *rtspAddress, uint32_t width, uint32_t height)
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

void DLL_LOCAL launchThread(void *args)
{

#if defined _WIN32

#else
    pthread_t thread_Id;

    uint32_t result = 0;
    result = pthread_create(&thread_Id, NULL, &ffmpegStartStreaming, args);

#endif
}

uint8_t DLL_PUBLIC isCapturing(uint8_t camId)
{
    if (isValidCamId(camId))
        return cams[camId].isCapturing;
}

size_t DLL_PUBLIC getFrameSize(uint8_t camId) {

    if (!rBuffer[camId]) {
        return 0;
    }

    return rBuffer[camId]->size;

}

// Lock ??!?!?
uint8_t DLL_PUBLIC getFrame(uint8_t camId, void *container) {

    if (!isValidCamId(camId))
        return 0;

    if (sizeof(container) < rBuffer[camId]->size)
        return -1;

    




}

uint8_t isValidCamId(uint8_t camId) {
    if (camId >= 0 && camId <= MAX_CAMS) {
        return 1;
    } else {
        return 0;
    }
}

void *ffmpegStartStreaming(void *args)
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
    fprintf(stderr, "In thread of cam #%d\n", cam->camId);

    formatContext = avformat_alloc_context();
    // Start ffmpeg process ...

    avformat_network_init();

    if (avformat_open_input(&formatContext, cam->rtspAddress, NULL, NULL) != 0)
    {
        printf("FATAL: Cam #%d cannot connect to %s\n", cam->camId, cam->rtspAddress);
        goto cleanup;
    }

    cam->isConnected = 1;
    printf("INFO: Cam #%d is connected to %s\n", cam->camId, cam->rtspAddress);

    if (avformat_find_stream_info(formatContext, NULL) < 0)
    {
        goto cleanup;
    }

    videoStreamId = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    if (videoStreamId < 0)
    {
        fprintf(stderr, "FATAL: Cam #%d -> Could not find a video stream!\n", cam->camId);
        goto cleanup;
    }

    // Try to find a h265 codec?
    codec = avcodec_find_decoder(formatContext->streams[videoStreamId]->codecpar->codec_id);
    if (!codec)
    {
        // This is pretty sad, cannot recover...
        fprintf(stderr, "FATAL: Cam #%d -> Cannot find a decoder for this stream, exiting\n", cam->camId);
        goto cleanup;
    }

    codecContext = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamId]->codecpar);

    // Open codec ...
    if (avcodec_open2(codecContext, codec, NULL) < 0)
    {
        fprintf(stderr, "FATAL: Cam #%d -> Error opening with avcodec_open2\n", cam->camId);
        goto cleanup;
    }

    frame = av_frame_alloc();
    convertFrame = av_frame_alloc();
    packet = av_packet_alloc();

    if (!frame || !packet)
    {
        fprintf(stderr, "FATAL: Cannot allocate frame or packet\n");
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
            fprintf(stderr, "FATAL: Cam #%d decoder error (%d)\n", cam->camId, result);
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
                fprintf(stderr, "FATAL: Cam %d -> Error while fetching frame\n", cam->camId);
                goto cleanup;
            }

            fprintf(stderr, "INFO: Cam #%d -> Frame %d (%dx%d), type = %c , size = %d bytes, format = %d (%s), pts %ld keyFrame %d\n",
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
                        1
                    );
                }

                convertFrame->width = cam->requestedWidth ? cam->requestedWidth : frame->width;
                convertFrame->height = cam->requestedHeight ? cam->requestedHeight : frame->height;

                // Actual scaling
                sws_scale(resize, (const uint8_t* const*)(frame->data), (frame->linesize), 0, frame->height, convertFrame->data, convertFrame->linesize);

                toWriteFrame = convertFrame;
            }
            else {
                toWriteFrame = frame;
            }

            if (toWriteFrame->format == -1) {
                toWriteFrame->format = AV_PIX_FMT_YUV420P;
            }

            imageBufferSize = av_image_get_buffer_size(toWriteFrame->format, toWriteFrame->width, toWriteFrame->height, 1);

            fprintf(stderr,"INFO: OUTPUT Cam #%d -> Frame format %d (%s), %dx%d, %d bytes ready to be consumed\n", 
                cam->camId,
                toWriteFrame->format,
                av_get_pix_fmt_name(toWriteFrame->format),
                toWriteFrame->width,
                toWriteFrame->height,
                imageBufferSize
            );


            finalFrameBuffer = av_malloc(imageBufferSize);
            av_image_copy_to_buffer(finalFrameBuffer, imageBufferSize,
                                      (const uint8_t * const *)toWriteFrame->data,
                                      (const int *)toWriteFrame->linesize, toWriteFrame->format,
                                      toWriteFrame->width, toWriteFrame->height, 1);


            // DEBUG Writing to view image?

            fp = fopen("dump.yuv","wb");
            fwrite(finalFrameBuffer, imageBufferSize, 1, fp);
            fclose(fp);
            

            if (!rBuffer[cam->camId]) {
                rBuffer[cam->camId] = malloc(sizeof(ringBuffer_t));
                rb_init(rBuffer[cam->camId], MAX_BUFFERED_FRAMES, imageBufferSize);
            }

            // Lock?!??!
            rb_push(rBuffer[cam->camId], finalFrameBuffer);

            // Write buffer into ringbuffer rdy for consumption.

            av_frame_unref(frame);
            if (convertFrame) {
                av_frame_unref(convertFrame);
            }

            // DEUBG
            cam->isEOS = 1;
        }
        av_packet_unref(packet);
    }

cleanup:
    fprintf(stderr, "Exiting thread (cleanup) of cam #%d\n", cam->camId);
    cam->isCapturing = 0;
    // Cleanup
    avformat_network_deinit();
    cam->isConnected = 0;
    avformat_close_input(&formatContext);
    avformat_free_context(formatContext);

    if (codec) {
        avcodec_close(codecContext);
        avcodec_free_context(&codecContext);
    }

    if (packet) {
        av_packet_free(&packet);
    }

    if (frame) {
        av_frame_free(&frame);
    }

    if (convertFrame) {
        av_frame_free(&convertFrame);
    }
    if (convertFrameBuffer) {
        av_free(convertFrameBuffer);
    }
    if (resize) {
        sws_freeContext(resize);
    }
    if (finalFrameBuffer) {
        av_free(finalFrameBuffer);
    }
    if (rBuffer[cam->camId]) {
        rb_free(rBuffer[cam->camId]);
        free(rBuffer[cam->camId]);
    }

    pthread_exit(0);
}
