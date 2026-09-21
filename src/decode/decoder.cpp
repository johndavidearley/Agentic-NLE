#include "decode/decoder.hpp"
#include "decode/ffmpeg_headers.hpp"
#include "media/probe.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
static_assert(LIBAVFORMAT_VERSION_MAJOR == 61 && LIBAVCODEC_VERSION_MAJOR == 61 &&
                  LIBAVUTIL_VERSION_MAJOR == 59 && LIBSWSCALE_VERSION_MAJOR == 8 &&
                  LIBSWRESAMPLE_VERSION_MAJOR == 5,
              "FFmpeg 7 development headers required");
namespace nle::decode {
namespace {
using Clock = std::chrono::steady_clock;
void check(int code, const char *operation) {
    if (code >= 0)
        return;
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    throw DomainError(std::string(operation) + ": " + text);
}
SourceTime absolute(SourceTime origin, RationalTime relative) {
    if (!origin.negative()) {
        const auto t = origin.magnitude() + relative;
        return {t.value(), t.rate()};
    }
    if (relative >= origin.magnitude()) {
        const auto t = relative - origin.magnitude();
        return {t.value(), t.rate()};
    }
    const auto t = origin.magnitude() - relative;
    return {-t.value(), t.rate()};
}
std::int64_t scaled(RationalTime time, std::int64_t rate, bool ceiling) {
    if (time.value() > std::numeric_limits<std::int64_t>::max() / rate)
        throw DomainError("Playback timestamp exceeds conversion limit");
    const auto product = time.value() * rate;
    return product / time.rate() + (ceiling && product % time.rate() != 0 ? 1 : 0);
}
std::int64_t ticks(SourceTime t, AVRational base) {
    // av_rescale handles the product without a 64-bit intermediate overflow.
    const auto m = t.magnitude();
    if (m.rate() > std::numeric_limits<int>::max())
        throw DomainError("Source time denominator exceeds decoder limit");
    return av_rescale_q_rnd(t.negative() ? -m.value() : m.value(),
                            AVRational{1, static_cast<int>(m.rate())}, base, AV_ROUND_DOWN);
}
} // namespace
std::int64_t sample_floor(RationalTime time) { return scaled(time, 48000, false); }
std::int64_t sample_ceil(RationalTime time) { return scaled(time, 48000, true); }
struct Decoder::State {
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *current = nullptr, *next = nullptr;
    SwsContext *scale = nullptr;
    SwrContext *resample = nullptr;
    std::atomic_bool &stop;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(5);
    MediaAsset asset;
    std::uint32_t stream;
    int width, height;
    bool is_video = false, ended = false, flushing = false, have_next = false;
    SourceTime origin, next_pts;
    std::optional<SourceTime> current_pts;
    RationalTime stream_end;
    std::int64_t audio_start = 0, audio_end = 0;
    bool audio_started = false, from_start = true;
    std::vector<float> audio;
    State(const MediaAsset &a, std::uint32_t index, int w, int h, std::atomic_bool &cancel)
        : stop(cancel), asset(a), stream(index), width(w), height(h) {}
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
        auto &s = *static_cast<State *>(opaque);
        return s.stop.load() || Clock::now() >= s.deadline;
    }
    void budget() const {
        if (stop.load())
            throw DomainError("Decode cancelled");
        if (Clock::now() >= deadline)
            throw DomainError("Source decode timed out");
    }
    void begin() {
        deadline = Clock::now() + std::chrono::seconds(5);
        budget();
    }
    bool decode() {
        av_frame_unref(next);
        for (;;) {
            budget();
            const int got = avcodec_receive_frame(codec, next);
            if (got == 0) {
                if (next->best_effort_timestamp == AV_NOPTS_VALUE)
                    throw DomainError("Decoded frame has no timestamp");
                const auto base = format->streams[stream]->time_base;
                next_pts =
                    SourceTime::from_ticks(next->best_effort_timestamp, {base.num, base.den});
                return true;
            }
            if (got == AVERROR_EOF)
                return false;
            if (got != AVERROR(EAGAIN))
                check(got, "Receive frame");
            if (flushing)
                return false;
            int read;
            do {
                budget();
                av_packet_unref(packet);
                read = av_read_frame(format, packet);
            } while (read >= 0 && packet->stream_index != static_cast<int>(stream));
            if (read == AVERROR_EOF) {
                flushing = true;
                check(avcodec_send_packet(codec, nullptr), "Flush decoder");
            } else {
                check(read, "Read packet");
                check(avcodec_send_packet(codec, packet), "Send packet");
                av_packet_unref(packet);
            }
        }
    }
    bool decode_audio() {
        const bool frame = decode();
        if (!frame && ended) {
            audio.clear();
            return false;
        }
        if (frame &&
            (next->sample_rate != codec->sample_rate || next->format != codec->sample_fmt ||
             av_channel_layout_compare(&next->ch_layout, &codec->ch_layout)))
            throw DomainError("Audio format changed within a stream");
        const auto delay = swr_get_delay(resample, codec->sample_rate);
        if (frame) {
            const auto offset = next_pts >= origin ? sample_floor(next_pts.since(origin))
                                                   : -sample_ceil(origin.since(next_pts));
            audio_start =
                offset - av_rescale_rnd(delay, 48000, codec->sample_rate, AV_ROUND_NEAR_INF);
            const auto base = format->streams[stream]->time_base;
            const auto uncertainty = sample_ceil({base.num, base.den});
            // Coarse container PTS (e.g. Matroska milliseconds) quantize contiguous
            // audio packets. Preserve sample continuity within one timestamp tick.
            if (audio_started && std::abs(audio_start - audio_end) <= uncertainty)
                audio_start = audio_end;
            audio_started = true;
        } else {
            audio_start = audio_end;
            ended = true;
        }
        const auto capacity = swr_get_out_samples(resample, frame ? next->nb_samples : 0);
        check(capacity, "Resample capacity");
        if (capacity > 48000)
            throw DomainError("Audio frame exceeds one second limit");
        audio.resize(static_cast<std::size_t>(capacity) * 2);
        std::uint8_t *destination = reinterpret_cast<std::uint8_t *>(audio.data());
        std::vector<const std::uint8_t *> planes;
        if (frame) {
            const int n =
                av_sample_fmt_is_planar(codec->sample_fmt) ? codec->ch_layout.nb_channels : 1;
            for (int i = 0; i < n; ++i)
                planes.push_back(next->extended_data[i]);
        }
        const auto count =
            swr_convert(resample, &destination, capacity, frame ? planes.data() : nullptr,
                        frame ? next->nb_samples : 0);
        check(count, "Resample");
        audio.resize(static_cast<std::size_t>(count) * 2);
        audio_end = audio_start + count;
        return frame || count > 0;
    }
};
Decoder::Decoder(const MediaAsset &asset, std::uint32_t stream, int width, int height,
                 std::atomic_bool &stop)
    : state_(std::make_unique<State>(asset, stream, width, height, stop)) {
    auto &s = *state_;
    if ((avformat_version() >> 16) != 61 || (avcodec_version() >> 16) != 61 ||
        (avutil_version() >> 16) != 59 || (swscale_version() >> 16) != 8 ||
        (swresample_version() >> 16) != 5)
        throw DomainError("FFmpeg runtime ABI does not match the development headers");
    if (!asset.source || media::source_status(asset) != media::SourceStatus::Available)
        throw DomainError("Source missing or changed; relink before playback");
    const auto found = std::find_if(asset.source->streams.begin(), asset.source->streams.end(),
                                    [&](const auto &item) { return item.index == stream; });
    if (found == asset.source->streams.end())
        throw DomainError("Selected source stream is unavailable");
    const auto location =
        std::find_if(asset.locations.begin(), asset.locations.end(),
                     [](const auto &item) { return item.role == LocationRole::Original; });
    s.origin = asset.source->time_mode == SourceTimeMode::SharedOrigin
                   ? source_origin(*asset.source)
                   : SourceTime::from_ticks(found->start_ticks, found->time_base);
    s.stream_end = stream_duration(*found, asset.source->container_duration);
    if (asset.source->time_mode == SourceTimeMode::SharedOrigin)
        s.stream_end = (found->duration_ticks != -1 || found->duration_estimate != RationalTime{})
                           ? s.stream_end + stream_offset(*asset.source, *found)
                           : source_duration(*asset.source);
    s.format = avformat_alloc_context();
    if (!s.format)
        throw std::bad_alloc();
    s.format->interrupt_callback = {State::interrupt, &s};
    AVDictionary *options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    av_dict_set(&options, "format_whitelist", "wav,mov,matroska,avi,flac,mp3,ogg,aiff", 0);
    av_dict_set(&options, "probesize", "5000000", 0);
    av_dict_set(&options, "analyzeduration", "5000000", 0);
    const auto opened = avformat_open_input(&s.format, location->uri.c_str(), nullptr, &options);
    av_dict_free(&options);
    check(opened, "Open source");
    check(avformat_find_stream_info(s.format, nullptr), "Read streams");
    if (stream >= s.format->nb_streams)
        throw DomainError("Source stream layout changed");
    const auto *p = s.format->streams[stream]->codecpar;
    s.is_video = found->kind == TrackKind::Video;
    if (p->codec_type != (s.is_video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO) ||
        found->codec != avcodec_get_name(p->codec_id))
        throw DomainError("Source stream changed; relink before playback");
    if (s.is_video) {
        if (p->width <= 0 || p->height <= 0 || p->width > 1920 || p->height > 1080)
            throw DomainError("Preview supports sources up to 1920x1080");
        if (p->color_trc == AVCOL_TRC_SMPTE2084 || p->color_trc == AVCOL_TRC_ARIB_STD_B67)
            throw DomainError("HDR preview is unsupported");
        const auto *matrix = av_packet_side_data_get(p->coded_side_data, p->nb_coded_side_data,
                                                     AV_PKT_DATA_DISPLAYMATRIX);
        if (matrix && matrix->size >= 9 * sizeof(std::int32_t) &&
            std::abs(av_display_rotation_get(
                reinterpret_cast<const std::int32_t *>(matrix->data))) > 0.01)
            throw DomainError("Rotated sources require normalization before preview");
    } else if (p->sample_rate < 8000 || p->sample_rate > 192000 || p->ch_layout.nb_channels < 1 ||
               p->ch_layout.nb_channels > 8)
        throw DomainError("Unsupported audio sample rate or layout");
    const auto *implementation = avcodec_find_decoder(p->codec_id);
    if (!implementation)
        throw DomainError("Decoder unavailable");
    s.codec = avcodec_alloc_context3(implementation);
    if (!s.codec)
        throw std::bad_alloc();
    check(avcodec_parameters_to_context(s.codec, p), "Copy codec parameters");
    s.codec->thread_count = 2;
    s.codec->max_pixels = 1920 * 1080;
    check(avcodec_open2(s.codec, implementation, nullptr), "Open decoder");
    s.packet = av_packet_alloc();
    s.current = av_frame_alloc();
    s.next = av_frame_alloc();
    if (!s.packet || !s.current || !s.next)
        throw std::bad_alloc();
    if (!s.is_video) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        check(swr_alloc_set_opts2(&s.resample, &stereo, AV_SAMPLE_FMT_FLT, 48000,
                                  &s.codec->ch_layout, s.codec->sample_fmt, s.codec->sample_rate, 0,
                                  nullptr),
              "Create resampler");
        check(swr_init(s.resample), "Initialize resampler");
    }
    s.budget();
}
Decoder::~Decoder() = default;
void Decoder::seek(RationalTime source) {
    auto &s = *state_;
    s.begin();
    // Starting audio at its origin gives resampling and coarse packet timestamps
    // a stable sample anchor after arbitrary seeks. Work remains deadline bounded.
    const auto preroll =
        s.is_video && source > RationalTime{1} ? source - RationalTime{1} : RationalTime{};
    const auto target = absolute(s.origin, preroll);
    // Negative indexed seeks can discard the initial packets in Matroska.
    if (target.negative() || preroll == RationalTime{}) {
        Decoder reopened(s.asset, s.stream, s.width, s.height, s.stop);
        state_ = std::move(reopened.state_);
        return;
    }
    const auto value = ticks(target, s.format->streams[s.stream]->time_base);
    check(avformat_seek_file(s.format, static_cast<int>(s.stream),
                             std::numeric_limits<std::int64_t>::min(), value, value,
                             AVSEEK_FLAG_BACKWARD),
          "Seek");
    avcodec_flush_buffers(s.codec);
    av_packet_unref(s.packet);
    av_frame_unref(s.current);
    av_frame_unref(s.next);
    s.have_next = s.ended = s.flushing = false;
    s.current_pts.reset();
    s.audio.clear();
    s.audio_start = s.audio_end = 0;
    s.audio_started = false;
    s.from_start = false;
    if (s.resample) {
        swr_close(s.resample);
        check(swr_init(s.resample), "Reset resampler");
    }
}
Video Decoder::video(RationalTime source) {
    auto &s = *state_;
    s.begin();
    if (!s.is_video)
        throw DomainError("Wrong decoder kind");
    if (source >= s.stream_end)
        return {};
    const auto target = absolute(s.origin, source);
    if (!s.have_next && !s.ended) {
        s.have_next = s.decode();
        s.ended = !s.have_next;
    }
    // Demuxer seek indexes may use DTS, landing after the requested PTS for
    // reordered pictures. Fall back to a bounded decode from the original prefix.
    if (!s.current_pts && s.have_next && s.next_pts > target && !s.from_start) {
        Decoder reopened(s.asset, s.stream, s.width, s.height, s.stop);
        state_ = std::move(reopened.state_);
        return video(source);
    }
    while (s.have_next && s.next_pts <= target) {
        s.budget();
        av_frame_unref(s.current);
        av_frame_move_ref(s.current, s.next);
        s.current_pts = s.next_pts;
        s.have_next = s.decode();
        s.ended = !s.have_next;
    }
    if (!s.current_pts || *s.current_pts < s.origin)
        return {};
    auto end = s.have_next ? s.next_pts.since(s.origin) : s.stream_end;
    if (!s.have_next && s.current->duration > 0) {
        const auto base = s.format->streams[s.stream]->time_base;
        const auto duration =
            SourceTime::from_ticks(s.current->duration, {base.num, base.den}).magnitude();
        end = std::min(end, s.current_pts->since(s.origin) + duration);
    }
    if (source >= end)
        return {};
    Video result{s.current_pts->since(s.origin), end, s.width, s.height,
                 std::vector<std::uint8_t>(static_cast<std::size_t>(s.width * s.height * 3))};
    if (s.current->width != s.codec->width || s.current->height != s.codec->height)
        throw DomainError("Video dimensions changed within a stream");
    auto sar = s.current->sample_aspect_ratio;
    if (sar.num <= 0 || sar.den <= 0)
        sar = {1, 1};
    const double aspect = static_cast<double>(s.current->width) * sar.num /
                          (static_cast<double>(s.current->height) * sar.den);
    const double box = static_cast<double>(s.width) / s.height;
    const int width =
        std::clamp(aspect >= box ? s.width : static_cast<int>(s.height * aspect), 1, s.width);
    const int height =
        std::clamp(aspect >= box ? static_cast<int>(s.width / aspect) : s.height, 1, s.height);
    s.scale = sws_getCachedContext(s.scale, s.current->width, s.current->height,
                                   static_cast<AVPixelFormat>(s.current->format), width, height,
                                   AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!s.scale)
        throw DomainError("Cannot create scaler");
    const int matrix = s.current->colorspace == AVCOL_SPC_BT709        ? SWS_CS_ITU709
                       : s.current->colorspace == AVCOL_SPC_SMPTE240M  ? SWS_CS_SMPTE240M
                       : s.current->colorspace == AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020
                                                                       : SWS_CS_DEFAULT;
    const auto *coefficients = sws_getCoefficients(matrix);
    check(sws_setColorspaceDetails(s.scale, coefficients,
                                   s.current->color_range == AVCOL_RANGE_JPEG, coefficients, 1, 0,
                                   1 << 16, 1 << 16),
          "Set source color matrix");
    std::uint8_t *planes[]{result.rgb.data() +
                           ((s.height - height) / 2 * s.width + (s.width - width) / 2) * 3};
    const int stride[]{s.width * 3};
    check(sws_scale(s.scale, s.current->data, s.current->linesize, 0, s.current->height, planes,
                    stride),
          "Scale frame");
    return result;
}
void Decoder::mix(std::int64_t source_sample, std::span<float> stereo, float gain) {
    auto &s = *state_;
    s.begin();
    if (s.is_video || stereo.size() % 2)
        throw DomainError("Invalid audio request");
    const auto end = std::min(source_sample + static_cast<std::int64_t>(stereo.size() / 2),
                              sample_ceil(s.stream_end));
    auto position = source_sample;
    while (position < end) {
        s.budget();
        if (s.audio.empty() || position >= s.audio_end) {
            if (!s.decode_audio())
                break;
            continue;
        }
        if (position < s.audio_start) {
            position = std::min(end, s.audio_start);
            continue;
        }
        const auto count = std::min(end - position, s.audio_end - position);
        for (std::int64_t i = 0; i < count * 2; ++i)
            stereo[static_cast<std::size_t>((position - source_sample) * 2 + i)] +=
                gain * s.audio[static_cast<std::size_t>((position - s.audio_start) * 2 + i)];
        position += count;
    }
}
} // namespace nle::decode
