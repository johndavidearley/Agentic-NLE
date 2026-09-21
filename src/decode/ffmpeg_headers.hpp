#pragma once
#ifdef __clang__
#pragma clang system_header
#elif defined(__GNUC__)
#pragma GCC system_header
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
