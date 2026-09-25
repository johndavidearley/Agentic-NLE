#pragma once
#include "project/model.hpp"
#include <atomic>
#include <memory>
#include <span>
namespace nle::decode {
struct Video {
    std::optional<RationalTime> pts, end;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgb;
};
// One local file and explicit stream. Instances stay on the decode thread.
class Decoder {
  public:
    Decoder(const MediaAsset &asset, std::uint32_t stream, int width, int height,
            std::atomic_bool &stop, bool require_sdr_bt709 = false);
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    void seek(RationalTime source);
    Video video(RationalTime source);
    void mix(std::int64_t source_sample, std::span<float> stereo, float gain);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
std::int64_t sample_floor(RationalTime time);
std::int64_t sample_ceil(RationalTime time);
} // namespace nle::decode
