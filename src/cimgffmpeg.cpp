/*

    pHash, the open source perceptual hash library
    Copyright (C) 2009 Aetilius, Inc.
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Evan Klinger - eklinger@phash.org
    D Grant Starkweather - dstarkweather@phash.org

*/

#include "cimgffmpeg.h"

void vfinfo_close(VFInfo *vfinfo) {
    if (vfinfo->pFormatCtx != NULL) {
        if (vfinfo->pCodecCtx) {
            avcodec_free_context(&vfinfo->pCodecCtx);
            vfinfo->pCodecCtx = NULL;
        }
        avformat_close_input(&vfinfo->pFormatCtx);
        vfinfo->pFormatCtx = NULL;
        vfinfo->width = -1;
        vfinfo->height = -1;
    }
}

int ReadFrames(VFInfo *st_info, CImgList<uint8_t> *pFrameList,
               unsigned int low_index, unsigned int hi_index) {
    // target pixel format
    AVPixelFormat ffmpeg_pixfmt;
    if (st_info->pixelformat == 0)
        ffmpeg_pixfmt = AV_PIX_FMT_GRAY8;
    else
        ffmpeg_pixfmt = AV_PIX_FMT_RGB24;

    st_info->next_index = low_index;

    if (st_info->pFormatCtx == NULL) {
        st_info->current_index = 0;

        av_log_set_level(AV_LOG_QUIET);

        // Open video file
        if (avformat_open_input(&st_info->pFormatCtx, st_info->filename, NULL,
                                NULL) != 0)
            return -1;  // Couldn't open file

        // Retrieve stream information
        if (avformat_find_stream_info(st_info->pFormatCtx, NULL) < 0)
            return -1;  // Couldn't find stream information

        // dump_format(pFormatCtx,0,NULL,0);//debugging function to print
        // infomation about format

        unsigned int i;
        // Find the video stream
        for (i = 0; i < st_info->pFormatCtx->nb_streams; i++) {
            if (st_info->pFormatCtx->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_VIDEO) {
                st_info->videoStream = i;
                break;
            }
        }
        if (st_info->videoStream == -1) return -1;  // no video stream

        // Allocate codec context for the video stream
        AVStream *video_stream = st_info->pFormatCtx->streams[st_info->videoStream];
        st_info->pCodec = avcodec_find_decoder(video_stream->codecpar->codec_id);
        if (!st_info->pCodec) return -1;
        st_info->pCodecCtx = avcodec_alloc_context3(st_info->pCodec);
        if (!st_info->pCodecCtx) return -1;
        if (avcodec_parameters_to_context(st_info->pCodecCtx, video_stream->codecpar) < 0) return -1;
        if (avcodec_open2(st_info->pCodecCtx, st_info->pCodec, NULL) < 0) return -1;
        st_info->height = (st_info->height <= 0) ? st_info->pCodecCtx->height : st_info->height;
        st_info->width = (st_info->width <= 0) ? st_info->pCodecCtx->width : st_info->width;
    }

    AVFrame *pFrame;

    // Allocate video frame
    pFrame = av_frame_alloc();
    if (pFrame == NULL) return -1;

    // Allocate an AVFrame structure
    AVFrame *pConvertedFrame = av_frame_alloc();
    if (pConvertedFrame == NULL) return -1;

    uint8_t *buffer;
    int numBytes;
    // Determine required buffer size and allocate buffer
    numBytes = av_image_get_buffer_size(ffmpeg_pixfmt, st_info->width, st_info->height, 1);
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    if (buffer == NULL) return -1;
    av_image_fill_arrays(pConvertedFrame->data, pConvertedFrame->linesize, buffer, ffmpeg_pixfmt, st_info->width, st_info->height, 1);

    int frameFinished;
    int size = 0;

    int channels = ffmpeg_pixfmt == AV_PIX_FMT_GRAY8 ? 1 : 3;

    AVPacket packet;
    int result = 1;
    CImg<uint8_t> next_image;
    SwsContext *c = sws_getContext(
        st_info->pCodecCtx->width, st_info->pCodecCtx->height,
        st_info->pCodecCtx->pix_fmt, st_info->width, st_info->height,
        ffmpeg_pixfmt, SWS_BICUBIC, NULL, NULL, NULL);
    while ((result >= 0) && (size < st_info->nb_retrieval) &&
           (st_info->current_index <= hi_index)) {
        result = av_read_frame(st_info->pFormatCtx, &packet);
        if (result < 0) break;
        if (packet.stream_index == st_info->videoStream) {
            if (avcodec_send_packet(st_info->pCodecCtx, &packet) == 0) {
                while (avcodec_receive_frame(st_info->pCodecCtx, pFrame) == 0) {
                    if (st_info->current_index == st_info->next_index) {
                        st_info->next_index += st_info->step;
                        sws_scale(c, pFrame->data, pFrame->linesize, 0,
                                  st_info->pCodecCtx->height, pConvertedFrame->data,
                                  pConvertedFrame->linesize);
                        next_image.assign(*pConvertedFrame->data, channels,
                                          st_info->width, st_info->height, 1, true);
                        next_image.permute_axes("yzcx");
                        pFrameList->push_back(next_image);
                        size++;
                    }
                    st_info->current_index++;
                }
            }
            av_packet_unref(&packet);
        }
    }

    if (result < 0) {
        if (st_info->pCodecCtx) {
            avcodec_free_context(&st_info->pCodecCtx);
            st_info->pCodecCtx = NULL;
        }
        avformat_close_input(&st_info->pFormatCtx);
        st_info->pFormatCtx = NULL;
        st_info->width = -1;
        st_info->height = -1;
    }

    av_free(buffer);
    buffer = NULL;
    av_free(pConvertedFrame);
    pConvertedFrame = NULL;
    av_free(pFrame);
    pFrame = NULL;
    sws_freeContext(c);
    c = NULL;

    return size;
}

int NextFrames(VFInfo *st_info, CImgList<uint8_t> *pFrameList) {
    AVPixelFormat ffmpeg_pixfmt;
    if (st_info->pixelformat == 0) {
        ffmpeg_pixfmt = AV_PIX_FMT_GRAY8;
    } else {
        ffmpeg_pixfmt = AV_PIX_FMT_RGB24;
    }

    if (st_info->pFormatCtx == NULL) {
        st_info->current_index = 0;
        st_info->next_index = 0;
        st_info->videoStream = -1;
        // st_info->pFormatCtx =
        // (AVFormatContext*)malloc(sizeof(AVFormatContext)); st_info->pCodecCtx
        // = (AVCodecContext*)malloc(sizeof(AVCodecContext)); st_info->pCodec =
        // (AVCodec*)malloc(sizeof(AVCodec));

        av_log_set_level(AV_LOG_QUIET);
        // Open video file
        if (avformat_open_input(&st_info->pFormatCtx, st_info->filename, NULL,
                                NULL) != 0) {
            return -1;  // Couldn't open file
        }

        // Retrieve stream information
        if (avformat_find_stream_info(st_info->pFormatCtx, NULL) < 0) {
            return -1;  // Couldn't find stream information
        }

        unsigned int i;

        // Find the video stream
        for (i = 0; i < st_info->pFormatCtx->nb_streams; i++) {
            if (st_info->pFormatCtx->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_VIDEO) {
                st_info->videoStream = i;
                break;
            }
        }

        if (st_info->videoStream == -1) {
            return -1;  // no video stream
        }

        // Allocate codec context for the video stream
        AVStream *video_stream = st_info->pFormatCtx->streams[st_info->videoStream];
        st_info->pCodec = avcodec_find_decoder(video_stream->codecpar->codec_id);
        if (!st_info->pCodec) return -1;
        st_info->pCodecCtx = avcodec_alloc_context3(st_info->pCodec);
        if (!st_info->pCodecCtx) return -1;
        if (avcodec_parameters_to_context(st_info->pCodecCtx, video_stream->codecpar) < 0) return -1;
        if (avcodec_open2(st_info->pCodecCtx, st_info->pCodec, NULL) < 0) return -1;

        st_info->width =
            (st_info->width <= 0) ? st_info->pCodecCtx->width : st_info->width;
        st_info->height = (st_info->height <= 0) ? st_info->pCodecCtx->height
                                                 : st_info->height;
    }

    AVFrame *pFrame;

    // Allocate video frame
    pFrame = av_frame_alloc();

    // Allocate an AVFrame structure
    AVFrame *pConvertedFrame = av_frame_alloc();
    if (pConvertedFrame == NULL) {
        return -1;
    }

    uint8_t *buffer;
    int numBytes;
    // Determine required buffer size and allocate buffer
    numBytes = av_image_get_buffer_size(ffmpeg_pixfmt, st_info->width, st_info->height, 1);
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    if (buffer == NULL) {
        return -1;
    }
    av_image_fill_arrays(pConvertedFrame->data, pConvertedFrame->linesize, buffer, ffmpeg_pixfmt, st_info->width, st_info->height, 1);

    int frameFinished;
    int size = 0;
    AVPacket packet;
    int result = 1;
    CImg<uint8_t> next_image;
    SwsContext *c = sws_getContext(
        st_info->pCodecCtx->width, st_info->pCodecCtx->height,
        st_info->pCodecCtx->pix_fmt, st_info->width, st_info->height,
        ffmpeg_pixfmt, SWS_BICUBIC, NULL, NULL, NULL);
    while ((result >= 0) && (size < st_info->nb_retrieval)) {
        result = av_read_frame(st_info->pFormatCtx, &packet);
        if (result < 0) break;
        if (packet.stream_index == st_info->videoStream) {
            int channels = ffmpeg_pixfmt == AV_PIX_FMT_GRAY8 ? 1 : 3;
            if (avcodec_send_packet(st_info->pCodecCtx, &packet) == 0) {
                while (avcodec_receive_frame(st_info->pCodecCtx, pFrame) == 0) {
                    if (st_info->current_index == st_info->next_index) {
                        st_info->next_index += st_info->step;

                        sws_scale(c, pFrame->data, pFrame->linesize, 0,
                                  st_info->pCodecCtx->height, pConvertedFrame->data,
                                  pConvertedFrame->linesize);

                        next_image.assign(*pConvertedFrame->data, channels,
                                          st_info->width, st_info->height, 1, true);
                        next_image.permute_axes("yzcx");
                        pFrameList->push_back(next_image);
                        size++;
                    }
                    st_info->current_index++;
                }
            }
        }
        av_packet_unref(&packet);
    }

    av_free(buffer);
    buffer = NULL;
    av_free(pConvertedFrame);
    pConvertedFrame = NULL;
    av_free(pFrame);
    pFrame = NULL;
    sws_freeContext(c);
    c = NULL;
    if (result < 0) {
        if (st_info->pCodecCtx) {
            avcodec_free_context(&st_info->pCodecCtx);
            st_info->pCodecCtx = NULL;
        }
        avformat_close_input(&st_info->pFormatCtx);
        st_info->pCodecCtx = NULL;
        st_info->pFormatCtx = NULL;
        st_info->pCodec = NULL;
        st_info->width = -1;
        st_info->height = -1;
    }
    return size;
}

int GetNumberStreams(const char *file) {
    AVFormatContext *pFormatCtx;
    av_log_set_level(AV_LOG_QUIET);
    // Open video file
    if (avformat_open_input(&pFormatCtx, file, NULL, NULL))
        return -1;  // Couldn't open file

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
        return -1;  // Couldn't find stream information
    int result = pFormatCtx->nb_streams;
    avformat_close_input(&pFormatCtx);
    return result;
}

long GetNumberVideoFrames(const char *file) {
    long nb_frames = -1;
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    av_log_set_level(AV_LOG_QUIET);
    // Open video file
    if (avformat_open_input(&pFormatCtx, file, NULL, NULL)) {
        // Couldn't open file
        goto freeContext;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        // Couldn't find stream information
        goto closeContext;
    }

    // Find the first video stream
    {
        int videoStream = -1;
        for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_VIDEO) {
                videoStream = i;
                break;
            }
        }

        if (videoStream == -1) {
            goto closeContext;
        }

        AVStream *str = pFormatCtx->streams[videoStream];

        nb_frames = str->nb_frames;

        if (nb_frames <= 0) {
            nb_frames = (long)av_index_search_timestamp(
                str, str->duration, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
        }

        if (nb_frames <= 0) {
            int timebase = str->time_base.den / str->time_base.num;
            nb_frames = str->duration / timebase;
        }
    }

closeContext:
    avformat_close_input(&pFormatCtx);
freeContext:
    avformat_free_context(pFormatCtx);

    return nb_frames;
}

float fps(const char *filename) {
    float result = 0;
    AVFormatContext *pFormatCtx = avformat_alloc_context();

    // Open video file
    if (avformat_open_input(&pFormatCtx, filename, NULL, NULL)) {
        avformat_free_context(pFormatCtx);
        return -1;  // Couldn't open file
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        return -1;  // Couldn't find stream information
    }

    // Find the first video stream
    int videoStream = -1;
    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    if (videoStream == -1) return -1;  // Didn't find a video stream

    int num = (pFormatCtx->streams[videoStream]->r_frame_rate).num;
    int den = (pFormatCtx->streams[videoStream]->r_frame_rate).den;
    result = num / den;

    avformat_close_input(&pFormatCtx);
    avformat_free_context(pFormatCtx);

    return result;
}
