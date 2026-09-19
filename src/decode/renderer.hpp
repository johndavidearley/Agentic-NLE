#pragma once
#include "decode/decoder.hpp"
#include "playback/sequence.hpp"
#include <filesystem>
namespace nle::decode {
struct Picture {
    RationalTime position;
    ClipId clip;
    Video video;
};
struct Chunk {
    std::int64_t sample = 0;
    std::vector<float> audio;
    std::vector<Picture> pictures;
    bool end = false;
    std::size_t bytes() const;
};
class Renderer {
  public:
    Renderer(playback::SequencePlan plan, RationalTime start, std::atomic_bool &stop);
    Chunk next();
    Picture poster(RationalTime time);
    const playback::SequencePlan &plan() const { return plan_; }

  private:
    struct Slot {
        ClipId clip;
        std::size_t media = 0;
        std::uint32_t stream = 0;
        std::unique_ptr<Decoder> decoder;
    };
    struct Identity {
        std::size_t media;
        std::filesystem::file_time_type modified;
    };
    playback::SequencePlan plan_;
    std::atomic_bool &stop_;
    std::vector<Slot> video_, audio_;
    std::vector<Identity> identities_;
    std::int64_t sample_ = 0, frame_ = 0;
    int width_ = 960, height_ = 540;
    unsigned blocks_ = 0;
    RationalTime frame_time() const;
    Decoder &decoder(const playback::Contribution &item, bool video);
    void check_sources() const;
};
} // namespace nle::decode
