#include "project/persistence.hpp"
#include "project/file_io.hpp"
#include <charconv>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace nle {
namespace {
constexpr std::size_t max_bytes = 16 * 1024 * 1024;
constexpr std::uint64_t max_records = 100000;
class Reader {
  public:
    explicit Reader(std::string_view data) : stream_(std::string(data)) {
        stream_.imbue(std::locale::classic());
    }
    void expect(std::string_view expected) {
        std::string actual;
        if (!(stream_ >> actual) || actual != expected)
            throw DomainError("expected native format token: " + std::string(expected));
    }
    template <typename T> T number() {
        std::string token;
        if (!(stream_ >> token))
            throw DomainError("missing number");
        T result{};
        const auto [end, error] =
            std::from_chars(token.data(), token.data() + token.size(), result);
        if (error != std::errc{} || end != token.data() + token.size())
            throw DomainError("invalid integer");
        return result;
    }
    std::uint64_t count() {
        const auto value = number<std::uint64_t>();
        if (value > remaining_)
            throw DomainError("native project record limit exceeded");
        remaining_ -= value;
        return value;
    }
    std::string text() {
        stream_ >> std::ws;
        if (stream_.peek() != '"')
            throw DomainError("expected quoted text");
        std::string value;
        if (!(stream_ >> std::quoted(value)) || value.size() > 4096)
            throw DomainError("invalid quoted text");
        return value;
    }
    RationalTime time() {
        const auto value = number<std::int64_t>();
        const auto rate = number<std::int64_t>();
        return {value, rate};
    }
    void end() {
        stream_ >> std::ws;
        if (!stream_.eof())
            throw DomainError("trailing project data");
    }

  private:
    std::istringstream stream_;
    std::uint64_t remaining_ = max_records;
};
void write_time(std::ostream &out, RationalTime time) { out << time.value() << ' ' << time.rate(); }
TrackKind track_kind(std::uint64_t value) {
    if (value > 1)
        throw DomainError("unknown track kind");
    return static_cast<TrackKind>(value);
}
MediaKind media_kind(std::uint64_t value) {
    if (value > 2)
        throw DomainError("unknown media kind");
    return static_cast<MediaKind>(value);
}
LocationRole location_role(std::uint64_t value) {
    if (value > 1)
        throw DomainError("unknown location role");
    return static_cast<LocationRole>(value);
}
} // namespace

std::string serialize(const ProjectSnapshot &project) {
    validate(project);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "NLE_PROJECT 4\nPROJECT " << project.id.value << ' ' << project.next_id << ' '
        << std::quoted(project.name) << "\nMEDIA " << project.media.size() << '\n';
    for (const auto &asset : project.media) {
        out << "ASSET " << asset.id.value << ' ' << static_cast<int>(asset.kind) << ' '
            << std::quoted(asset.name) << ' ';
        write_time(out, asset.duration);
        out << ' ' << asset.locations.size() << '\n';
        for (const auto &location : asset.locations)
            out << "LOCATION " << static_cast<int>(location.role) << ' '
                << std::quoted(location.uri) << '\n';
        out << "SOURCE " << (asset.source ? 1 : 0) << '\n';
        if (asset.source) {
            const auto &source = *asset.source;
            out << "METADATA " << std::quoted(source.container) << ' '
                << std::quoted(source.probe_version) << ' '
                << std::quoted(source.probe_configuration) << ' ' << source.byte_size << ' ';
            write_time(out, source.container_duration);
            out << ' ' << source.streams.size() << '\n';
            out << "CLOCK " << static_cast<int>(source.time_mode) << ' '
                << (source.container_start ? 1 : 0);
            if (source.container_start) {
                const auto value = source.container_start->magnitude();
                out << ' ' << (source.container_start->negative() ? -value.value() : value.value())
                    << ' ' << value.rate();
            }
            out << '\n';
            for (const auto &stream : source.streams) {
                out << "STREAM " << stream.index << ' ' << static_cast<int>(stream.kind) << ' '
                    << std::quoted(stream.codec) << ' ';
                write_time(out, stream.time_base);
                out << ' ' << stream.duration_ticks << ' ' << (stream.start_known ? 1 : 0) << ' '
                    << stream.start_ticks << ' ';
                write_time(out, stream.frame_duration);
                out << ' ';
                write_time(out, stream.nominal_frame_duration);
                out << ' ' << stream.width << ' ' << stream.height << ' ' << stream.sample_rate
                    << ' ' << stream.channels << ' ';
                write_time(out, stream.duration_estimate);
                out << '\n';
            }
        }
    }
    out << "SEQUENCES " << project.sequences.size() << '\n';
    for (const auto &sequence : project.sequences) {
        out << "SEQUENCE " << sequence.id.value << ' ' << std::quoted(sequence.name) << ' ';
        write_time(out, sequence.frame_duration);
        out << ' ' << sequence.tracks.size() << '\n';
        for (const auto &track : sequence.tracks) {
            out << "TRACK " << track.id.value << ' ' << static_cast<int>(track.kind) << ' '
                << std::quoted(track.name) << ' ' << track.clips.size() << '\n';
            for (const auto &clip : track.clips) {
                out << "CLIP " << clip.id.value << ' ' << clip.media.value << ' ';
                write_time(out, clip.position);
                out << ' ';
                write_time(out, clip.source.start);
                out << ' ';
                write_time(out, clip.source.duration);
                out << '\n';
            }
        }
    }
    out << "AUDIT " << project.revision << ' ' << project.operations.size() << '\n';
    for (const auto &operation : project.operations) {
        out << "OP " << operation.id.value << ' ' << operation.revision << ' '
            << static_cast<int>(operation.kind) << ' ' << operation.target.value << ' '
            << static_cast<int>(operation.actor.kind) << ' '
            << std::quoted(operation.actor.id.value) << ' ' << std::quoted(operation.label) << ' '
            << operation.actions.size() << '\n';
        for (const auto &action : operation.actions)
            out << "ACTION " << std::quoted(action) << '\n';
    }
    out << "END\n";
    auto data = out.str();
    if (data.size() > max_bytes)
        throw DomainError("native project exceeds 16 MiB");
    // Enforce the reader's record budget on output too: every saved file is reloadable.
    (void)deserialize(data);
    return data;
}

ProjectSnapshot deserialize(std::string_view data) {
    if (data.size() > max_bytes)
        throw DomainError("native project exceeds 16 MiB");
    Reader reader(data);
    reader.expect("NLE_PROJECT");
    const auto version = reader.number<unsigned>();
    if (version != 1 && version != 2 && version != 3 && version != 4)
        throw DomainError("unsupported project version");
    reader.expect("PROJECT");
    ProjectSnapshot project;
    project.id = ProjectId{reader.number<std::uint64_t>()};
    project.next_id = reader.number<std::uint64_t>();
    project.name = reader.text();
    reader.expect("MEDIA");
    const auto media_count = reader.count();
    for (std::uint64_t i = 0; i < media_count; ++i) {
        reader.expect("ASSET");
        MediaAsset asset;
        asset.id = MediaId{reader.number<std::uint64_t>()};
        asset.kind = media_kind(reader.number<std::uint64_t>());
        asset.name = reader.text();
        asset.duration = reader.time();
        const auto locations = reader.count();
        for (std::uint64_t j = 0; j < locations; ++j) {
            reader.expect("LOCATION");
            const auto role = location_role(reader.number<std::uint64_t>());
            asset.locations.push_back({role, reader.text()});
        }
        if (version >= 3) {
            reader.expect("SOURCE");
            const auto present = reader.number<unsigned>();
            if (present > 1)
                throw DomainError("invalid source flag");
            if (present) {
                reader.expect("METADATA");
                SourceMetadata source;
                source.container = reader.text();
                source.probe_version = reader.text();
                source.probe_configuration = reader.text();
                source.byte_size = reader.number<std::uint64_t>();
                source.container_duration = reader.time();
                const auto streams = reader.count();
                if (version >= 4) {
                    reader.expect("CLOCK");
                    const auto mode = reader.number<unsigned>();
                    if (mode > 1)
                        throw DomainError("invalid source time convention");
                    source.time_mode = static_cast<SourceTimeMode>(mode);
                    const auto known = reader.number<unsigned>();
                    if (known > 1)
                        throw DomainError("invalid container origin flag");
                    if (known) {
                        const auto value = reader.number<std::int64_t>();
                        const auto rate = reader.number<std::int64_t>();
                        source.container_start = SourceTime{value, rate};
                    }
                }
                if (streams > 64)
                    throw DomainError("too many source streams");
                for (std::uint64_t j = 0; j < streams; ++j) {
                    reader.expect("STREAM");
                    SourceStream stream;
                    stream.index = reader.number<std::uint32_t>();
                    stream.kind = track_kind(reader.number<unsigned>());
                    stream.codec = reader.text();
                    stream.time_base = reader.time();
                    stream.duration_ticks = reader.number<std::int64_t>();
                    const auto known = reader.number<unsigned>();
                    if (known > 1)
                        throw DomainError("invalid stream start flag");
                    stream.start_known = known != 0;
                    stream.start_ticks = reader.number<std::int64_t>();
                    stream.frame_duration = reader.time();
                    stream.nominal_frame_duration = reader.time();
                    stream.width = reader.number<std::uint32_t>();
                    stream.height = reader.number<std::uint32_t>();
                    stream.sample_rate = reader.number<std::uint32_t>();
                    stream.channels = reader.number<std::uint32_t>();
                    if (version >= 4)
                        stream.duration_estimate = reader.time();
                    source.streams.push_back(std::move(stream));
                }
                asset.source = std::move(source);
            }
        }
        project.media.push_back(std::move(asset));
    }
    reader.expect("SEQUENCES");
    const auto sequences = reader.count();
    for (std::uint64_t i = 0; i < sequences; ++i) {
        reader.expect("SEQUENCE");
        Sequence sequence;
        sequence.id = SequenceId{reader.number<std::uint64_t>()};
        sequence.name = reader.text();
        sequence.frame_duration = reader.time();
        const auto tracks = reader.count();
        for (std::uint64_t j = 0; j < tracks; ++j) {
            reader.expect("TRACK");
            Track track;
            track.id = TrackId{reader.number<std::uint64_t>()};
            track.kind = track_kind(reader.number<std::uint64_t>());
            track.name = reader.text();
            const auto clips = reader.count();
            for (std::uint64_t k = 0; k < clips; ++k) {
                reader.expect("CLIP");
                Clip clip;
                clip.id = ClipId{reader.number<std::uint64_t>()};
                clip.media = MediaId{reader.number<std::uint64_t>()};
                clip.position = reader.time();
                clip.source.start = reader.time();
                clip.source.duration = reader.time();
                track.clips.push_back(clip);
            }
            sequence.tracks.push_back(std::move(track));
        }
        project.sequences.push_back(std::move(sequence));
    }
    if (version >= 2) {
        reader.expect("AUDIT");
        project.revision = reader.number<std::uint64_t>();
        const auto operations = reader.count();
        if (operations > max_operations)
            throw DomainError("too many operations");
        for (std::uint64_t i = 0; i < operations; ++i) {
            reader.expect("OP");
            OperationRecord operation;
            operation.id = OperationId{reader.number<std::uint64_t>()};
            operation.revision = reader.number<std::uint64_t>();
            const auto kind = reader.number<unsigned>();
            if (kind > 2)
                throw DomainError("invalid change kind");
            operation.kind = static_cast<ChangeKind>(kind);
            operation.target = OperationId{reader.number<std::uint64_t>()};
            const auto actor_kind = reader.number<unsigned>();
            if (actor_kind > 2)
                throw DomainError("invalid actor kind");
            operation.actor.kind = static_cast<ActorKind>(actor_kind);
            operation.actor.id.value = reader.text();
            operation.label = reader.text();
            const auto actions = reader.count();
            if (actions > max_batch_commands)
                throw DomainError("too many batch actions");
            for (std::uint64_t j = 0; j < actions; ++j) {
                reader.expect("ACTION");
                operation.actions.push_back(reader.text());
            }
            project.operations.push_back(std::move(operation));
        }
    }
    // Version 1 migrates explicitly to revision zero with unknown historical attribution.
    reader.expect("END");
    reader.end();
    validate(project);
    return project;
}

void save_project(const ProjectSnapshot &project, const std::filesystem::path &path) {
    detail::atomic_write_file(serialize(project), path);
}

void detail::atomic_write_file(std::string_view data, const std::filesystem::path &path) {
    auto parent = path.parent_path();
    if (parent.empty())
        parent = ".";
    // Reserve a unique adjacent directory atomically; never share a fixed temporary file.
    std::random_device random;
    std::filesystem::path staging;
    for (int attempt = 0; attempt < 16; ++attempt) {
        staging = parent / path.filename();
        staging += ".tmp-" + std::to_string(random());
        if (std::filesystem::create_directory(staging))
            break;
        staging.clear();
    }
    if (staging.empty())
        throw DomainError("cannot reserve save staging directory");
    const auto temporary = staging / "project";
    try {
        std::ofstream output(temporary, std::ios::binary);
        if (!output)
            throw DomainError("cannot open temporary project");
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output)
            throw DomainError("project write failed");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw DomainError("project replacement failed: " + std::to_string(GetLastError()));
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(staging, ignored);
        throw;
    }
    std::error_code ignored;
    std::filesystem::remove(staging, ignored);
}

ProjectSnapshot load_project(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw DomainError("cannot open project file");
    std::string data(max_bytes + 1, '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (input.bad())
        throw DomainError("project read failed");
    data.resize(static_cast<std::size_t>(input.gcount()));
    return deserialize(data);
}
} // namespace nle
