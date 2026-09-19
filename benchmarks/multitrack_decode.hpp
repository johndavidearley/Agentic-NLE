#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace nle::prototype {
struct Video {
    std::int64_t pts_us = -1;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgb;
};
// Prototype-only decoder, never used by the shipping desktop.
class Decoder {
  public:
    Decoder(const std::string &file, bool video, std::atomic_bool &stop);
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    void seek(std::int64_t source_us);
    Video video(std::int64_t source_us);
    void mix(std::int64_t source_sample, std::span<float> stereo, float gain);
    static std::string version();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace nle::prototype
