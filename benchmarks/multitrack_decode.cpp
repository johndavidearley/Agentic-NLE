#include "multitrack_decode.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
namespace nle::prototype {
namespace {
constexpr AVRational us{1, 1000000}, samples{1, 48000};
using Clock = std::chrono::steady_clock;
void check(int result, const char *operation) {
    if (result >= 0)
        return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, message, sizeof(message));
    throw std::runtime_error(std::string(operation) + ": " + message);
}
} // namespace
struct Decoder::State {
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *current = nullptr, *next = nullptr;
    SwsContext *scale = nullptr;
    SwrContext *resample = nullptr;
    std::atomic_bool &stop;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(5);
    int stream = -1;
    bool is_video, ended = false, flushing = false, have_next = false;
    std::int64_t origin = 0, next_pts = 0, current_pts = -1, audio_start = 0;
    std::vector<float> audio;
    std::string file;
    explicit State(bool video, std::atomic_bool &cancel) : stop(cancel), is_video(video) {}
    ~State() {
        swr_free(&resample);
        sws_freeContext(scale);
        av_frame_free(&current);
        av_frame_free(&next);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    static int interrupt(void *opaque) {
        const auto &self = *static_cast<State *>(opaque);
        return self.stop.load() || Clock::now() >= self.deadline;
    }
    bool decode() {
        deadline = Clock::now() + std::chrono::seconds(5);
        av_frame_unref(next);
        while (!stop.load()) {
            const auto received = avcodec_receive_frame(codec, next);
            if (received == 0) {
                if (next->best_effort_timestamp == AV_NOPTS_VALUE)
                    throw std::runtime_error("Decoded frame has no timestamp");
                next_pts = av_rescale_q(next->best_effort_timestamp,
                                        format->streams[stream]->time_base, us) -
                           origin;
                return true;
            }
            if (received == AVERROR_EOF)
                return false;
            if (received != AVERROR(EAGAIN))
                check(received, "receive frame");
            if (flushing)
                return false;
            int read = 0;
            do {
                av_packet_unref(packet);
                read = av_read_frame(format, packet);
            } while (read >= 0 && packet->stream_index != stream && !stop.load());
            if (read == AVERROR_EOF) {
                flushing = true;
                check(avcodec_send_packet(codec, nullptr), "flush decoder");
            } else {
                check(read, "read packet");
                check(avcodec_send_packet(codec, packet), "send packet");
                av_packet_unref(packet);
            }
        }
        throw std::runtime_error("Decode cancelled");
    }
    bool decode_audio() {
        if (!decode()) {
            ended = true;
            audio.clear();
            return false;
        }
        const auto delay = swr_get_delay(resample, codec->sample_rate);
        audio_start = av_rescale_q(next_pts, us, samples) -
                      av_rescale_q(delay, AVRational{1, codec->sample_rate}, samples);
        const int capacity = swr_get_out_samples(resample, next->nb_samples);
        check(capacity, "resample size");
        if (capacity > 48000)
            throw std::runtime_error("Audio block exceeds prototype limit");
        audio.resize(static_cast<std::size_t>(capacity) * 2);
        std::uint8_t *destination = reinterpret_cast<std::uint8_t *>(audio.data());
        std::vector<const std::uint8_t *> planes;
        const int plane_count =
            av_sample_fmt_is_planar(codec->sample_fmt) ? codec->ch_layout.nb_channels : 1;
        for (int plane = 0; plane < plane_count; ++plane)
            planes.push_back(next->extended_data[plane]);
        const auto count =
            swr_convert(resample, &destination, capacity, planes.data(), next->nb_samples);
        check(count, "resample");
        audio.resize(static_cast<std::size_t>(count) * 2);
        return true;
    }
};
Decoder::Decoder(const std::string &file, bool video, std::atomic_bool &stop)
    : state_(std::make_unique<State>(video, stop)) {
    auto &s = *state_;
    s.file = file;
    s.format = avformat_alloc_context();
    if (!s.format)
        throw std::bad_alloc();
    s.format->interrupt_callback = {State::interrupt, &s};
    AVDictionary *options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const auto opened = avformat_open_input(&s.format, file.c_str(), nullptr, &options);
    av_dict_free(&options);
    check(opened, "open source");
    check(avformat_find_stream_info(s.format, nullptr), "read streams");
    s.stream = av_find_best_stream(s.format, video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO, -1,
                                   -1, nullptr, 0);
    check(s.stream, "select stream");
    s.origin = s.format->start_time == AV_NOPTS_VALUE ? 0 : s.format->start_time;
    const auto *parameters = s.format->streams[s.stream]->codecpar;
    const auto *implementation = avcodec_find_decoder(parameters->codec_id);
    if (!implementation)
        throw std::runtime_error("Decoder unavailable");
    s.codec = avcodec_alloc_context3(implementation);
    if (!s.codec)
        throw std::bad_alloc();
    check(avcodec_parameters_to_context(s.codec, parameters), "copy codec parameters");
    s.codec->thread_count = 2;
    check(avcodec_open2(s.codec, implementation, nullptr), "open decoder");
    s.packet = av_packet_alloc();
    s.current = av_frame_alloc();
    s.next = av_frame_alloc();
    if (!s.packet || !s.current || !s.next)
        throw std::bad_alloc();
    if (video) {
        if (s.codec->width <= 0 || s.codec->height <= 0 || s.codec->width > 1920 ||
            s.codec->height > 1080)
            throw std::runtime_error("Prototype video exceeds 1920x1080 profile");
    } else {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        check(swr_alloc_set_opts2(&s.resample, &stereo, AV_SAMPLE_FMT_FLT, 48000,
                                  &s.codec->ch_layout, s.codec->sample_fmt, s.codec->sample_rate, 0,
                                  nullptr),
              "create resampler");
        check(swr_init(s.resample), "initialize resampler");
    }
}
Decoder::~Decoder() = default;
void Decoder::seek(std::int64_t source_us) {
    // Some demuxers clamp negative indexed seeks to zero. Reopen to retain the
    // original negative prefix, then decode forward to the requested source time.
    if (state_->origin + source_us < 0) {
        Decoder reopened(state_->file, state_->is_video, state_->stop);
        state_ = std::move(reopened.state_);
        return;
    }
    auto &s = *state_;
    s.deadline = Clock::now() + std::chrono::seconds(5);
    const auto target =
        av_rescale_q(source_us + s.origin, us, s.format->streams[s.stream]->time_base);
    check(avformat_seek_file(s.format, s.stream, std::numeric_limits<std::int64_t>::min(), target,
                             target, AVSEEK_FLAG_BACKWARD),
          "seek");
    avcodec_flush_buffers(s.codec);
    av_packet_unref(s.packet);
    av_frame_unref(s.current);
    av_frame_unref(s.next);
    s.have_next = s.ended = s.flushing = false;
    s.current_pts = -1;
    s.audio.clear();
    if (s.resample) {
        swr_close(s.resample);
        check(swr_init(s.resample), "reset resampler");
    }
}
Video Decoder::video(std::int64_t source_us) {
    auto &s = *state_;
    if (!s.is_video)
        throw std::runtime_error("Wrong decoder kind");
    if (!s.have_next && !s.ended) {
        s.have_next = s.decode();
        s.ended = !s.have_next;
    }
    while (s.have_next && s.next_pts <= source_us) {
        av_frame_unref(s.current);
        av_frame_move_ref(s.current, s.next);
        s.current_pts = s.next_pts;
        s.have_next = s.decode();
        s.ended = !s.have_next;
    }
    if (s.current_pts < 0)
        return {};
    Video result{s.current_pts, 960, 540, std::vector<std::uint8_t>(960 * 540 * 3)};
    const double aspect = static_cast<double>(s.current->width) / s.current->height;
    const int width = aspect >= 960.0 / 540.0 ? 960 : static_cast<int>(540 * aspect);
    const int height = aspect >= 960.0 / 540.0 ? static_cast<int>(960 / aspect) : 540;
    s.scale = sws_getCachedContext(s.scale, s.current->width, s.current->height,
                                   static_cast<AVPixelFormat>(s.current->format), width, height,
                                   AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!s.scale)
        throw std::runtime_error("Cannot create scaler");
    std::uint8_t *planes[]{result.rgb.data() + ((540 - height) / 2 * 960 + (960 - width) / 2) * 3};
    const int stride[]{960 * 3};
    check(sws_scale(s.scale, s.current->data, s.current->linesize, 0, s.current->height, planes,
                    stride),
          "scale frame");
    return result;
}
void Decoder::mix(std::int64_t source_sample, std::span<float> stereo, float gain) {
    auto &s = *state_;
    if (s.is_video || stereo.size() % 2 != 0)
        throw std::runtime_error("Invalid audio request");
    const auto end = source_sample + static_cast<std::int64_t>(stereo.size() / 2);
    auto position = source_sample;
    while (position < end && !s.stop.load()) {
        if (s.audio.empty() ||
            position >= s.audio_start + static_cast<std::int64_t>(s.audio.size() / 2)) {
            if (s.ended || !s.decode_audio())
                break;
            continue;
        }
        if (position < s.audio_start) {
            position = std::min(end, s.audio_start);
            continue;
        }
        const auto count =
            std::min(end - position,
                     s.audio_start + static_cast<std::int64_t>(s.audio.size() / 2) - position);
        for (std::int64_t frame = 0; frame < count; ++frame)
            for (std::int64_t channel = 0; channel < 2; ++channel)
                stereo[static_cast<std::size_t>((position - source_sample + frame) * 2 +
                                                channel)] +=
                    gain * s.audio[static_cast<std::size_t>((position - s.audio_start + frame) * 2 +
                                                            channel)];
        position += count;
    }
}
std::string Decoder::version() { return av_version_info(); }
} // namespace nle::prototype
