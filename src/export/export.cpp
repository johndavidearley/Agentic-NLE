#include "export/export.hpp"
#include "decode/ffmpeg_headers.hpp"
#include "decode/renderer.hpp"
#include "media/probe.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nle::exporting {
namespace {
struct FormatDeleter {
    void operator()(AVFormatContext *value) const { avformat_free_context(value); }
};
struct CodecDeleter {
    void operator()(AVCodecContext *value) const { avcodec_free_context(&value); }
};
struct FrameDeleter {
    void operator()(AVFrame *value) const { av_frame_free(&value); }
};
struct PacketDeleter {
    void operator()(AVPacket *value) const { av_packet_free(&value); }
};
struct ScaleDeleter {
    void operator()(SwsContext *value) const { sws_freeContext(value); }
};
using Format = std::unique_ptr<AVFormatContext, FormatDeleter>;
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using Scale = std::unique_ptr<SwsContext, ScaleDeleter>;

[[noreturn]] void fail_ffmpeg(int code, const char *operation) {
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    throw DomainError(std::string(operation) + ": " + text);
}
void check(int code, const char *operation) {
    if (code < 0)
        fail_ffmpeg(code, operation);
}
bool has_pixel_format(const AVCodec *codec, AVPixelFormat format) {
    const void *supported = nullptr;
    check(avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &supported,
                                       nullptr),
          "Query video encoder pixel formats");
    if (!supported)
        return true;
    const auto *formats = static_cast<const AVPixelFormat *>(supported);
    for (auto item = formats; *item != AV_PIX_FMT_NONE; ++item)
        if (*item == format)
            return true;
    return false;
}
bool has_sample_format(const AVCodec *codec, AVSampleFormat format) {
    const void *supported = nullptr;
    check(avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &supported,
                                       nullptr),
          "Query audio encoder sample formats");
    if (!supported)
        return true;
    const auto *formats = static_cast<const AVSampleFormat *>(supported);
    for (auto item = formats; *item != AV_SAMPLE_FMT_NONE; ++item)
        if (*item == format)
            return true;
    return false;
}
std::int64_t frame_count(const playback::SequencePlan &plan) {
    if (plan.duration == RationalTime{})
        throw DomainError("Cannot export an empty sequence");
    if (plan.duration.rate() > INT_MAX || plan.frame_duration.value() > INT_MAX ||
        plan.frame_duration.rate() > INT_MAX)
        throw DomainError("Output time base exceeds FFmpeg limits");
    return av_rescale_q_rnd(plan.duration.value(),
                            AVRational{1, static_cast<int>(plan.duration.rate())},
                            AVRational{static_cast<int>(plan.frame_duration.value()),
                                       static_cast<int>(plan.frame_duration.rate())},
                            static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
}
std::string lower_extension(const std::filesystem::path &path) {
    auto result = path.extension().string();
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}
std::filesystem::path target_path(const Options &options) {
    if (options.destination.empty() || options.destination.filename().empty())
        throw DomainError("Export destination must name a file");
    const auto extension = lower_extension(options.destination);
    if ((options.preset == Preset::LosslessReference && extension != ".mkv") ||
        (options.preset == Preset::Mp4H264 && extension != ".mp4"))
        throw DomainError("Choose an .mkv lossless reference or .mp4 H.264 delivery file");
    auto absolute = std::filesystem::absolute(options.destination).lexically_normal();
    auto parent = absolute.parent_path();
    if (parent.empty())
        parent = std::filesystem::current_path();
    const auto canonical_parent = std::filesystem::canonical(parent);
    if (!std::filesystem::is_directory(canonical_parent))
        throw DomainError("Export folder is unavailable");
    const auto target = canonical_parent / absolute.filename();
    std::error_code error;
    const auto status = std::filesystem::symlink_status(target, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw DomainError("Cannot inspect export destination");
    if (status.type() != std::filesystem::file_type::not_found) {
        if (!std::filesystem::is_regular_file(status))
            throw DomainError("Export destination must be a regular file");
        if (!options.overwrite)
            throw DomainError("Export destination exists; select overwrite explicitly");
    }
    return target;
}
struct Staging {
    std::filesystem::path directory, output;
    explicit Staging(const std::filesystem::path &target, const char *extension) {
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            directory = target.parent_path() / target.filename();
            directory += ".nle-export-" + std::to_string(random());
            if (std::filesystem::create_directory(directory))
                break;
            directory.clear();
        }
        if (directory.empty())
            throw DomainError("Cannot reserve an export staging directory");
        output = directory / (std::string("render") + extension);
    }
    ~Staging() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
void install(const std::filesystem::path &staged, const std::filesystem::path &target,
             bool overwrite) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(target, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw DomainError("Cannot recheck export destination");
    if (status.type() != std::filesystem::file_type::not_found) {
        if (!std::filesystem::is_regular_file(status))
            throw DomainError("Export destination changed to a non-file");
        if (!overwrite)
            throw DomainError("Export destination appeared during export; choose it explicitly");
    }
#ifdef _WIN32
    const DWORD flags =
        overwrite ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH : MOVEFILE_WRITE_THROUGH;
    if (!MoveFileExW(staged.c_str(), target.c_str(), flags))
        throw DomainError("Cannot install completed export: " + std::to_string(GetLastError()));
#else
    if (overwrite) {
        if (::rename(staged.c_str(), target.c_str()) != 0)
            throw DomainError("Cannot install completed export");
    } else {
        if (::link(staged.c_str(), target.c_str()) != 0)
            throw DomainError("Export destination appeared or cannot be installed");
        (void)::unlink(staged.c_str());
    }
#endif
}
void set_discard_padding(AVPacket *packet, std::uint32_t samples) {
    std::size_t size = 0;
    auto *side_data = av_packet_get_side_data(packet, AV_PKT_DATA_SKIP_SAMPLES, &size);
    if (!side_data) {
        side_data = av_packet_new_side_data(packet, AV_PKT_DATA_SKIP_SAMPLES, 10);
        size = side_data ? 10 : 0;
    }
    if (!side_data || size < 10)
        throw DomainError("Cannot attach AAC end padding metadata");
    for (unsigned i = 0; i < 4; ++i)
        side_data[4 + i] = static_cast<std::uint8_t>(samples >> (i * 8));
}
struct Muxer {
    Format format;
    Codec video;
    Codec audio;
    AVStream *video_stream = nullptr, *audio_stream = nullptr;
    Frame video_frame;
    Frame audio_frame;
    Packet packet;
    Packet audio_pending_packet;
    Scale scaler;
    std::string video_codec_name, video_preset;
    std::int64_t audio_samples_total = 0;
    ~Muxer() {
        if (format && format->pb)
            avio_closep(&format->pb);
    }

    void write_packet(AVPacket *value, AVCodecContext *codec, AVStream *stream) {
        av_packet_rescale_ts(value, codec->time_base, stream->time_base);
        value->stream_index = stream->index;
        check(av_interleaved_write_frame(format.get(), value), "Write encoded packet");
        av_packet_unref(value);
    }
    void packets(AVCodecContext *codec, AVStream *stream, std::atomic_bool &stop) {
        for (;;) {
            if (stop.load(std::memory_order_relaxed))
                throw DomainError("Export cancelled");
            const auto status = avcodec_receive_packet(codec, packet.get());
            if (status == AVERROR(EAGAIN))
                break;
            if (status == AVERROR_EOF) {
                if (codec == audio.get() && codec->codec_id == AV_CODEC_ID_AAC &&
                    audio_pending_packet && audio_pending_packet->data) {
                    if (audio_pending_packet->pts == AV_NOPTS_VALUE)
                        throw DomainError("AAC encoder omitted the final packet timestamp");
                    const AVRational samples{1, 48000};
                    const auto packet_start =
                        av_rescale_q(audio_pending_packet->pts, codec->time_base, samples);
                    const auto packet_length =
                        av_rescale_q(codec->frame_size, codec->time_base, samples);
                    const auto padding = packet_start + packet_length - audio_samples_total;
                    if (padding < 0)
                        throw DomainError("AAC encoder ended before the sequence audio boundary");
                    if (padding > UINT32_MAX)
                        throw DomainError("AAC end padding exceeds FFmpeg limits");
                    audio_pending_packet->duration = packet_length - padding;
                    set_discard_padding(audio_pending_packet.get(),
                                        static_cast<std::uint32_t>(padding));
                    write_packet(audio_pending_packet.get(), codec, stream);
                }
                break;
            }
            check(status, "Receive encoded packet");
            if (codec == audio.get() && codec->codec_id == AV_CODEC_ID_AAC) {
                if (audio_pending_packet->data)
                    write_packet(audio_pending_packet.get(), codec, stream);
                av_packet_move_ref(audio_pending_packet.get(), packet.get());
            } else {
                write_packet(packet.get(), codec, stream);
            }
        }
    }
    void encode(AVCodecContext *codec, AVStream *stream, AVFrame *frame, std::atomic_bool &stop) {
        if (stop.load(std::memory_order_relaxed))
            throw DomainError("Export cancelled");
        auto status = avcodec_send_frame(codec, frame);
        if (status == AVERROR(EAGAIN)) {
            packets(codec, stream, stop);
            status = avcodec_send_frame(codec, frame);
        }
        check(status, "Submit frame to encoder");
        packets(codec, stream, stop);
    }
    void flush(AVCodecContext *codec, AVStream *stream, std::atomic_bool &stop) {
        auto status = avcodec_send_frame(codec, nullptr);
        if (status == AVERROR(EAGAIN)) {
            packets(codec, stream, stop);
            status = avcodec_send_frame(codec, nullptr);
        }
        if (status != AVERROR_EOF)
            check(status, "Flush encoder");
        packets(codec, stream, stop);
    }
};
AVCodecContext *make_video(Muxer &muxer, Preset preset, const playback::SequencePlan &plan,
                           AVRational time_base) {
    const AVCodec *encoder = nullptr;
    if (preset == Preset::LosslessReference) {
        encoder = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    } else {
        encoder = avcodec_find_encoder_by_name("libx264");
#ifdef _WIN32
        if (!encoder)
            encoder = avcodec_find_encoder_by_name("h264_mf");
#endif
    }
    if (!encoder)
        throw DomainError(preset == Preset::LosslessReference
                              ? "The linked FFmpeg build has no FFV1 encoder"
                              : "The linked FFmpeg build has no supported H.264 encoder");
    muxer.video_codec_name = encoder->name;
    muxer.video_preset = preset == Preset::LosslessReference       ? "FFV1 BGR0 8-bit"
                         : std::string(encoder->name) == "libx264" ? "libx264 CRF 18 medium"
                                                                   : "h264_mf quality 85";
    muxer.video_stream = avformat_new_stream(muxer.format.get(), nullptr);
    if (!muxer.video_stream)
        throw std::bad_alloc();
    muxer.video.reset(avcodec_alloc_context3(encoder));
    if (!muxer.video)
        throw std::bad_alloc();
    auto *context = muxer.video.get();
    context->codec_type = AVMEDIA_TYPE_VIDEO;
    context->codec_id = encoder->id;
    context->width = static_cast<int>(plan.output.width);
    context->height = static_cast<int>(plan.output.height);
    context->time_base = time_base;
    context->framerate = AVRational{time_base.den, time_base.num};
    context->thread_count = 2;
    context->gop_size = preset == Preset::LosslessReference ? 1 : 48;
    context->max_b_frames = preset == Preset::LosslessReference ? 0 : 2;
    if (preset == Preset::LosslessReference) {
        if (!has_pixel_format(encoder, AV_PIX_FMT_BGR0))
            throw DomainError("FFV1 encoder does not support lossless BGR0 output");
        context->pix_fmt = AV_PIX_FMT_BGR0;
        context->color_range = AVCOL_RANGE_JPEG;
        context->colorspace = AVCOL_SPC_RGB;
    } else {
        if (!has_pixel_format(encoder, AV_PIX_FMT_YUV420P))
            throw DomainError("Selected H.264 encoder does not support 8-bit YUV 4:2:0");
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        context->color_range = AVCOL_RANGE_MPEG;
        context->colorspace = AVCOL_SPC_BT709;
    }
    context->color_primaries = AVCOL_PRI_BT709;
    context->color_trc = AVCOL_TRC_BT709;
    if (muxer.format->oformat->flags & AVFMT_GLOBALHEADER)
        context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary *options = nullptr;
    if (preset == Preset::Mp4H264) {
        if (std::string(encoder->name) == "libx264") {
            av_dict_set(&options, "preset", "medium", 0);
            av_dict_set(&options, "crf", "18", 0);
            av_dict_set(&options, "profile", "high", 0);
        } else {
            av_dict_set(&options, "rate_control", "quality", 0);
            av_dict_set(&options, "quality", "85", 0);
        }
    } else
        context->level = 3;
    const auto result = avcodec_open2(context, encoder, &options);
    const auto *unused_video_option = av_dict_get(options, "", nullptr, AV_DICT_IGNORE_SUFFIX);
    const std::string unused_video_option_name =
        unused_video_option ? unused_video_option->key : "";
    av_dict_free(&options);
    check(result, "Open video encoder");
    if (!unused_video_option_name.empty())
        throw DomainError("Unsupported video encoder option: " + unused_video_option_name);
    check(avcodec_parameters_from_context(muxer.video_stream->codecpar, context),
          "Set video stream parameters");
    muxer.video_stream->time_base = context->time_base;
    muxer.video_stream->avg_frame_rate = context->framerate;
    muxer.video_frame.reset(av_frame_alloc());
    if (!muxer.video_frame)
        throw std::bad_alloc();
    auto *frame = muxer.video_frame.get();
    frame->format = context->pix_fmt;
    frame->width = context->width;
    frame->height = context->height;
    frame->color_range = context->color_range;
    frame->colorspace = context->colorspace;
    frame->color_primaries = context->color_primaries;
    frame->color_trc = context->color_trc;
    check(av_frame_get_buffer(frame, 32), "Allocate output frame");
    const auto flags = preset == Preset::LosslessReference ? SWS_POINT : SWS_BICUBIC;
    muxer.scaler.reset(sws_getContext(context->width, context->height, AV_PIX_FMT_RGB24,
                                      context->width, context->height, context->pix_fmt, flags,
                                      nullptr, nullptr, nullptr));
    if (!muxer.scaler)
        throw DomainError("Cannot prepare export color conversion");
    if (preset == Preset::Mp4H264) {
        const auto *coefficients = sws_getCoefficients(SWS_CS_ITU709);
        check(sws_setColorspaceDetails(muxer.scaler.get(), coefficients, 1, coefficients, 0, 0,
                                       1 << 16, 1 << 16),
              "Set BT.709 export conversion");
    }
    return context;
}
AVCodecContext *make_audio(Muxer &muxer, Preset preset, std::int64_t total_samples) {
    const AVCodec *encoder = preset == Preset::LosslessReference
                                 ? avcodec_find_encoder(AV_CODEC_ID_PCM_F32LE)
                                 : avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!encoder)
        throw DomainError("The linked FFmpeg build has no required audio encoder");
    const auto format =
        preset == Preset::LosslessReference ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP;
    if (!has_sample_format(encoder, format))
        throw DomainError("The linked audio encoder lacks its required sample format");
    muxer.audio_stream = avformat_new_stream(muxer.format.get(), nullptr);
    if (!muxer.audio_stream)
        throw std::bad_alloc();
    muxer.audio.reset(avcodec_alloc_context3(encoder));
    if (!muxer.audio)
        throw std::bad_alloc();
    auto *context = muxer.audio.get();
    context->codec_type = AVMEDIA_TYPE_AUDIO;
    context->codec_id = encoder->id;
    context->sample_rate = 48000;
    context->sample_fmt = format;
    context->time_base = AVRational{1, 48000};
    av_channel_layout_default(&context->ch_layout, 2);
    if (preset == Preset::Mp4H264) {
        context->bit_rate = 192000;
        context->profile = FF_PROFILE_AAC_LOW;
    }
    if (muxer.format->oformat->flags & AVFMT_GLOBALHEADER)
        context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(context, encoder, nullptr), "Open audio encoder");
    if (preset == Preset::Mp4H264) {
        if (context->frame_size <= 0)
            throw DomainError("AAC encoder has no fixed frame size");
        const auto frames = (total_samples + context->frame_size - 1) / context->frame_size;
        context->trailing_padding = static_cast<int>(frames * context->frame_size - total_samples);
    }
    check(avcodec_parameters_from_context(muxer.audio_stream->codecpar, context),
          "Set audio stream parameters");
    muxer.audio_stream->time_base = context->time_base;
    muxer.audio_frame.reset(av_frame_alloc());
    if (!muxer.audio_frame)
        throw std::bad_alloc();
    auto *frame = muxer.audio_frame.get();
    frame->format = context->sample_fmt;
    frame->sample_rate = context->sample_rate;
    frame->ch_layout = context->ch_layout;
    frame->nb_samples = preset == Preset::LosslessReference ? 480 : context->frame_size;
    check(av_frame_get_buffer(frame, 0), "Allocate audio frame");
    return context;
}
void write_video(Muxer &muxer, AVCodecContext *codec, const decode::Picture &picture,
                 std::int64_t frame_number, std::atomic_bool &stop) {
    auto *frame = muxer.video_frame.get();
    check(av_frame_make_writable(frame), "Prepare output frame");
    const auto expected =
        static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height) * 3;
    if (picture.video.width != frame->width || picture.video.height != frame->height ||
        picture.video.rgb.size() != expected)
        throw DomainError("Renderer returned an invalid full-resolution frame");
    const std::uint8_t *source[]{picture.video.rgb.data()};
    const int source_stride[]{frame->width * 3};
    if (sws_scale(muxer.scaler.get(), source, source_stride, 0, frame->height, frame->data,
                  frame->linesize) != frame->height)
        throw DomainError("Cannot convert rendered frame");
    frame->pts = frame_number;
    frame->pict_type = AV_PICTURE_TYPE_NONE;
    muxer.encode(codec, muxer.video_stream, frame, stop);
}
void write_pcm(Muxer &muxer, AVCodecContext *codec, std::span<const float> samples,
               std::int64_t start, std::atomic_bool &stop) {
    if (samples.empty() || samples.size() % 2)
        throw DomainError("Renderer returned invalid stereo PCM");
    auto *frame = muxer.audio_frame.get();
    const auto count = static_cast<int>(samples.size() / 2);
    if (count > frame->nb_samples)
        throw DomainError("PCM block exceeds the reference encoder frame");
    check(av_frame_make_writable(frame), "Prepare PCM frame");
    frame->nb_samples = count;
    std::memcpy(frame->data[0], samples.data(), samples.size_bytes());
    frame->pts = start;
    muxer.encode(codec, muxer.audio_stream, frame, stop);
}
void write_aac(Muxer &muxer, AVCodecContext *codec, std::vector<float> &pending,
               std::span<const float> samples, std::int64_t &encoded, std::atomic_bool &stop) {
    pending.insert(pending.end(), samples.begin(), samples.end());
    const auto frame_size = static_cast<std::size_t>(codec->frame_size);
    const auto interleaved = frame_size * 2;
    while (pending.size() >= interleaved) {
        auto *frame = muxer.audio_frame.get();
        check(av_frame_make_writable(frame), "Prepare AAC frame");
        auto *left = reinterpret_cast<float *>(frame->data[0]);
        auto *right = reinterpret_cast<float *>(frame->data[1]);
        for (std::size_t i = 0; i < frame_size; ++i) {
            left[i] = pending[i * 2];
            right[i] = pending[i * 2 + 1];
        }
        frame->nb_samples = static_cast<int>(frame_size);
        frame->pts = encoded;
        muxer.encode(codec, muxer.audio_stream, frame, stop);
        encoded += codec->frame_size;
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(interleaved));
    }
}
void finish_aac(Muxer &muxer, AVCodecContext *codec, std::vector<float> &pending,
                std::int64_t &encoded, std::atomic_bool &stop) {
    if (pending.empty())
        return;
    auto *frame = muxer.audio_frame.get();
    check(av_frame_make_writable(frame), "Prepare final AAC frame");
    auto *left = reinterpret_cast<float *>(frame->data[0]);
    auto *right = reinterpret_cast<float *>(frame->data[1]);
    const auto valid = pending.size() / 2;
    for (std::size_t i = 0; i < static_cast<std::size_t>(codec->frame_size); ++i) {
        left[i] = i < valid ? pending[i * 2] : 0.0F;
        right[i] = i < valid ? pending[i * 2 + 1] : 0.0F;
    }
    frame->nb_samples = codec->frame_size;
    frame->pts = encoded;
    muxer.encode(codec, muxer.audio_stream, frame, stop);
    encoded += codec->frame_size;
    pending.clear();
}
} // namespace

Result export_sequence(playback::SequencePlan plan, const Options &options,
                       std::atomic_bool &stop) {
    if (stop.load(std::memory_order_relaxed))
        throw DomainError("Export cancelled");
    const auto target = target_path(options);
    const auto total_frames = frame_count(plan);
    const auto total_samples = decode::sample_ceil(plan.duration);
    const AVRational time_base{static_cast<int>(plan.frame_duration.value()),
                               static_cast<int>(plan.frame_duration.rate())};
    AVRational reduced{};
    av_reduce(&reduced.num, &reduced.den, time_base.num, time_base.den, INT_MAX);
    if (reduced.num <= 0 || reduced.den <= 0)
        throw DomainError("Sequence frame rate cannot be represented by the encoder");
    const std::string extension = options.preset == Preset::LosslessReference ? ".mkv" : ".mp4";
    Staging staging(target, extension.c_str());

    const char *container = options.preset == Preset::LosslessReference ? "matroska" : "mp4";
    Format format;
    AVFormatContext *raw = nullptr;
    check(avformat_alloc_output_context2(&raw, nullptr, container, staging.output.string().c_str()),
          "Create export container");
    format.reset(raw);
    if (!format)
        throw std::bad_alloc();
    const auto video_id =
        options.preset == Preset::LosslessReference ? AV_CODEC_ID_FFV1 : AV_CODEC_ID_H264;
    const auto audio_id =
        options.preset == Preset::LosslessReference ? AV_CODEC_ID_PCM_F32LE : AV_CODEC_ID_AAC;
    if (avformat_query_codec(format->oformat, video_id, FF_COMPLIANCE_NORMAL) == 0 ||
        avformat_query_codec(format->oformat, audio_id, FF_COMPLIANCE_NORMAL) == 0)
        throw DomainError("The selected container does not support the export codecs");
    Muxer muxer;
    muxer.format = std::move(format);
    muxer.audio_samples_total = total_samples;
    auto *video = make_video(muxer, options.preset, plan, AVRational{reduced.num, reduced.den});
    auto *audio = make_audio(muxer, options.preset, total_samples);
    muxer.packet.reset(av_packet_alloc());
    muxer.audio_pending_packet.reset(av_packet_alloc());
    if (!muxer.packet || !muxer.audio_pending_packet)
        throw std::bad_alloc();
    av_dict_set(&muxer.format->metadata, "encoder", av_version_info(), 0);
    const auto revision = std::to_string(plan.revision);
    av_dict_set(&muxer.format->metadata, "nle_project_revision", revision.c_str(), 0);
    const auto sequence = std::to_string(plan.id.value);
    av_dict_set(&muxer.format->metadata, "nle_sequence_id", sequence.c_str(), 0);
    av_dict_set(&muxer.format->metadata, "nle_sdr", "BT.709", 0);
    av_dict_set(&muxer.format->metadata, "video_encoder", muxer.video_codec_name.c_str(), 0);
    av_dict_set(&muxer.format->metadata, "video_preset", muxer.video_preset.c_str(), 0);
    av_dict_set(&muxer.format->metadata, "audio_preset",
                options.preset == Preset::Mp4H264 ? "AAC-LC 192 kbit/s" : "PCM float 48 kHz stereo",
                0);
    if (!(muxer.format->oformat->flags & AVFMT_NOFILE))
        check(avio_open(&muxer.format->pb, staging.output.string().c_str(), AVIO_FLAG_WRITE),
              "Open staged export");
    AVDictionary *mux_options = nullptr;
    if (options.preset == Preset::Mp4H264) {
        av_dict_set(&mux_options, "movie_timescale", "48000", 0);
        av_dict_set(&mux_options, "movflags", "use_metadata_tags", 0);
    }
    const auto header_status = avformat_write_header(muxer.format.get(), &mux_options);
    const auto *unused_option = av_dict_get(mux_options, "", nullptr, AV_DICT_IGNORE_SUFFIX);
    const std::string unused_option_name = unused_option ? unused_option->key : "";
    av_dict_free(&mux_options);
    check(header_status, "Write export header");
    if (!unused_option_name.empty())
        throw DomainError("Unsupported muxer option: " + std::string(unused_option_name));

    decode::Renderer renderer(plan, RationalTime{}, stop, decode::RenderResolution::SequenceOutput);
    std::vector<float> pending;
    std::int64_t frames = 0, samples = 0, aac_encoded = 0;
    for (;;) {
        if (stop.load(std::memory_order_relaxed))
            throw DomainError("Export cancelled");
        auto chunk = renderer.next();
        if (chunk.end)
            break;
        if (chunk.sample != samples)
            throw DomainError("Renderer produced discontinuous PCM");
        if (options.preset == Preset::LosslessReference)
            write_pcm(muxer, audio, chunk.audio, chunk.sample, stop);
        else
            write_aac(muxer, audio, pending, chunk.audio, aac_encoded, stop);
        samples += static_cast<std::int64_t>(chunk.audio.size() / 2);
        for (const auto &picture : chunk.pictures) {
            if (frames >= total_frames)
                throw DomainError("Renderer exceeded the output frame count");
            write_video(muxer, video, picture, frames, stop);
            ++frames;
        }
        if (options.progress)
            options.progress({plan.revision, frames, total_frames, samples, total_samples});
    }
    if (samples != total_samples || frames != total_frames)
        throw DomainError("Rendered frame or audio sample count does not match the sequence");
    if (options.preset == Preset::Mp4H264)
        finish_aac(muxer, audio, pending, aac_encoded, stop);
    renderer.validate_sources();
    if (stop.load(std::memory_order_relaxed))
        throw DomainError("Export cancelled");
    muxer.flush(video, muxer.video_stream, stop);
    muxer.flush(audio, muxer.audio_stream, stop);
    check(av_write_trailer(muxer.format.get()), "Finish export container");
    if (muxer.format->pb)
        check(avio_closep(&muxer.format->pb), "Close staged export");
    renderer.validate_sources();
    if (stop.load(std::memory_order_relaxed))
        throw DomainError("Export cancelled");
    install(staging.output, target, options.overwrite);
    return {
        plan.revision,          plan.duration,
        total_frames,           total_samples,
        muxer.video_codec_name, options.preset == Preset::LosslessReference ? "pcm_f32le" : "aac",
        av_version_info()};
}
} // namespace nle::exporting
