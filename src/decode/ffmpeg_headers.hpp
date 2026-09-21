#pragma once
// Route FFmpeg includes through this wrapper so the prepared SDK headers can be forced ahead of
// ambient system installs while still treating the external headers as system headers for warnings.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif
