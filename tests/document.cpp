#include "project/document.hpp"
#include "commands/editor.hpp"
#include <fstream>
#include <iostream>
#include <random>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace nle;
namespace fs = std::filesystem;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error("check failed: " #x);                                         \
    } while (false)
template <class F> void rejected(F action, const std::string &code) {
    try {
        action();
    } catch (const FileError &e) {
        CHECK(e.code == code);
        return;
    }
    throw std::runtime_error("expected file error: " + code);
}
struct Scratch {
    fs::path root, path;
    explicit Scratch(const fs::path &parent) : root(fs::canonical(parent)) {
        path = root / ("document-tests-" + std::to_string(std::random_device{}()));
        CHECK(fs::create_directory(path));
        CHECK(fs::canonical(path).parent_path() == root);
    }
    ~Scratch() {
        std::error_code error;
        if (fs::weakly_canonical(path, error).parent_path() == root && !error)
            fs::remove_all(path, error);
    }
};
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        Scratch scratch{fs::path(argv[1])};
        const auto file = scratch.path / fs::path(u8"project 日本.nle");
        Editor editor("Recovery fixture");
        const auto base = editor.snapshot();
        {
            DocumentFile fresh(file, true, true);
            CHECK(!fresh.exists());
            fresh.save(base);
            CHECK(fresh.exists() && fresh.load() == base);
            rejected([&] { DocumentFile competing(file, true); }, "project_locked");
            DocumentFile inspection(file, false);
            CHECK(inspection.load() == base);
            rejected([&] { inspection.save(base); }, "permission_denied");
            rejected([&] { inspection.checkpoint(base); }, "permission_denied");
        }
        editor.execute(CreateSequence{"First checkpoint"});
        const auto first = editor.snapshot();
        editor.execute(CreateSequence{"Second checkpoint"});
        const auto second = editor.snapshot();
        {
            DocumentFile document(file, true);
            document.checkpoint(first);
            document.checkpoint(second);
            CHECK(load_project(file) == base);
            CHECK(document.recovery().project == second);
            std::ofstream(document.recovery_path(1), std::ios::binary | std::ios::trunc)
                << "truncated";
            const auto fallback = document.recovery();
            CHECK(fallback.project == first && !fallback.warning.empty());
            document.checkpoint(second);
            CHECK(document.recovery().project == second);
#ifdef _WIN32
            const auto target = document.recovery_path(0);
            HANDLE handle = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(handle != INVALID_HANDLE_VALUE);
            editor.execute(CreateSequence{"Third checkpoint"});
            try {
                rejected([&] { document.checkpoint(editor.snapshot()); }, "recovery_failed");
            } catch (...) {
                CloseHandle(handle);
                throw;
            }
            CloseHandle(handle);
            CHECK(document.recovery().project == second);
            handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(handle != INVALID_HANDLE_VALUE);
            try {
                rejected([&] { document.save(second); }, "save_failed");
            } catch (...) {
                CloseHandle(handle);
                throw;
            }
            CloseHandle(handle);
            CHECK(load_project(file) == base && document.recovery().project == second);
#endif
            document.save(second);
            CHECK(load_project(file) == second);
            CHECK(!document.recovery().project);
            CHECK(!fs::exists(document.recovery_path(0)) && !fs::exists(document.recovery_path(1)));
        }
        {
            DocumentFile document(file, true);
            editor.execute(CreateSequence{"External conflict"});
            save_project(editor.snapshot(), file); // Simulate a noncooperating writer.
            rejected([&] { document.save(second); }, "save_conflict");
            rejected([&] { document.checkpoint(second); }, "save_conflict");
            CHECK(load_project(file) == editor.snapshot());
        }
        const auto empty = scratch.path / "new.nle";
        {
            DocumentFile document(empty, true, true);
            save_project(base, empty); // File appeared after opening an unsaved destination.
            rejected([&] { document.save(second); }, "save_conflict");
            CHECK(load_project(empty) == base);
        }
        const auto draft = scratch.path / "draft.nle";
        {
            DocumentFile document(draft, true, true);
            document.checkpoint(first);
            CHECK(!fs::exists(draft));
        }
        {
            DocumentFile document(draft, true, true);
            CHECK(document.recovery().project == first);
            rejected([&] { document.save(first); }, "recovery_available");
            rejected([&] { document.checkpoint(first); }, "recovery_available");
            CHECK(document.recover() == first);
            document.discard_recovery();
            CHECK(!document.recovery().project);
            fs::create_directory(document.recovery_path(0));
            rejected([&] { document.checkpoint(first); }, "recovery_failed");
            CHECK(!fs::exists(draft));
            fs::remove(document.recovery_path(0));
        }
        {
            DocumentFile document(empty, true);
            document.checkpoint(first);
            fs::copy_file(document.recovery_path(0), scratch.path / "other.nle.recovery-0");
            Editor other("Another project");
            save_project(other.snapshot(), scratch.path / "other.nle");
            DocumentFile foreign(scratch.path / "other.nle", true);
            CHECK(!foreign.recovery().project && !foreign.recovery().warning.empty());
            // A directory at the second slot cannot turn a successful save into a failed one.
            fs::create_directory(document.recovery_path(1));
            document.save(first);
            CHECK(document.load() == first && !document.cleanup_warning().empty());
            fs::remove(document.recovery_path(1));
        }
        {
            DocumentFile copy(scratch.path / "copy.nle", true, true);
            copy.save(second);
            CHECK(copy.load().id == second.id && copy.load() == second);
        }
        std::cout << "Shared writer ownership, conflict-safe saves, two-slot recovery, corruption "
                     "fallback, drafts and failed writes passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
