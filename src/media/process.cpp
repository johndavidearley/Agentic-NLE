#include "media/process.hpp"
#include <array>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
namespace nle::media {
std::filesystem::path utf8_path(const std::string &text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
std::string path_utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
static bool stop_requested(const ProcessOptions &options) {
    return options.stop && options.stop->get().load(std::memory_order_relaxed);
}
namespace {
std::filesystem::path resolve_executable(const std::filesystem::path &requested) {
    if (requested.has_parent_path())
        return std::filesystem::absolute(requested);
#ifdef _WIN32
    const auto length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring paths(length, L'\0');
    if (length) {
        const auto copied = GetEnvironmentVariableW(L"PATH", paths.data(), length);
        if (copied >= length)
            throw DomainError("PATH changed while resolving executable");
        paths.resize(copied);
    }
    constexpr wchar_t separator = L';';
    auto name = requested;
    if (!name.has_extension())
        name += L".exe";
#else
    const auto *environment = std::getenv("PATH");
    const std::string paths = environment ? environment : "";
    constexpr char separator = ':';
    const auto name = requested;
#endif
    std::size_t start = 0;
    while (start < paths.size()) {
        const auto end = paths.find(separator, start);
        const auto directory = std::filesystem::path(paths.substr(start, end - start));
        if (directory.is_absolute()) {
            const auto candidate = directory / name;
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error)
                return candidate;
        }
        if (end == paths.npos)
            break;
        start = end + 1;
    }
    throw DomainError("probe executable not found on PATH; supply an explicit executable path");
}
#ifdef _WIN32
struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
std::wstring quote(const std::wstring &arg) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#else
struct Descriptor {
    int value = -1;
    ~Descriptor() {
        if (value >= 0)
            close(value);
    }
};
#endif
} // namespace
std::string run_process(const std::filesystem::path &requested,
                        const std::vector<std::string> &arguments, ProcessOptions options) {
    if (options.timeout.count() <= 0 || options.max_output == 0 ||
        options.max_output > 16 * 1024 * 1024)
        throw DomainError("invalid process limits");
    if (stop_requested(options))
        throw DomainError("probe cancelled");
    const auto executable = resolve_executable(requested);
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    std::string output;
    std::array<char, 4096> buffer{};
    const auto append = [&](std::size_t size) {
        if (size > options.max_output - output.size())
            throw DomainError("probe output limit exceeded");
        output.append(buffer.data(), size);
    };
    const auto check = [&] {
        if (stop_requested(options))
            throw DomainError("probe cancelled");
        if (std::chrono::steady_clock::now() >= deadline)
            throw DomainError("probe timed out");
    };
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle reader, writer, input, job, process, thread;
    if (!CreatePipe(&reader.value, &writer.value, &security, 0) ||
        !SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0))
        throw DomainError("cannot create probe pipe");
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (input.value == INVALID_HANDLE_VALUE || !job.value ||
        !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
        throw DomainError("cannot initialize probe process");
    auto command = quote(executable.wstring());
    for (const auto &arg : arguments) {
        if (arg.find('\0') != std::string::npos)
            throw DomainError("NUL in process argument");
        command += L' ' + quote(utf8_path(arg).wstring());
    }
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<unsigned char> attribute_storage(attribute_bytes);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes))
        throw DomainError("cannot initialize process handle list");
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
    } attribute_cleanup{attributes};
    HANDLE inherited[] = {input.value, writer.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr))
        throw DomainError("cannot restrict inherited process handles");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = writer.value;
    startup.StartupInfo.hStdError = writer.value;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        nullptr, &startup.StartupInfo, &info))
        throw DomainError("cannot start probe executable: " + std::to_string(GetLastError()));
    process.value = info.hProcess;
    thread.value = info.hThread;
    if (!AssignProcessToJobObject(job.value, process.value) ||
        ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, INFINITE);
        throw DomainError("cannot supervise probe process");
    }
    CloseHandle(writer.value);
    writer.value = nullptr;
    DWORD code = STILL_ACTIVE;
    bool finished = false;
    try {
        for (;;) {
            check();
            DWORD available = 0;
            if (!PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() != ERROR_BROKEN_PIPE)
                    throw DomainError("probe pipe failed");
                available = 0;
            }
            if (available) {
                DWORD read = 0;
                if (!ReadFile(reader.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                              nullptr))
                    throw DomainError("probe read failed");
                append(read);
                continue;
            }
            if (finished)
                break;
            const auto waited = WaitForSingleObject(process.value, 0);
            if (waited == WAIT_FAILED)
                throw DomainError("probe wait failed");
            if (waited == WAIT_OBJECT_0) {
                if (!GetExitCodeProcess(process.value, &code))
                    throw DomainError("probe exit status unavailable");
                // Drain again after observing exit: the last write may follow the prior peek.
                finished = true;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {
        TerminateJobObject(job.value, 1);
        WaitForSingleObject(process.value, INFINITE);
        throw;
    }
    if (code != 0)
        throw DomainError("probe exited with code " + std::to_string(code) + ": " +
                          output.substr(0, 512));
#else
    int pipes[2];
    if (pipe(pipes) != 0)
        throw DomainError("cannot create probe pipe");
    Descriptor reader{pipes[0]}, writer{pipes[1]};
    if (fcntl(reader.value, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(writer.value, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(reader.value, F_SETFL, O_NONBLOCK) < 0)
        throw DomainError("cannot configure probe pipe");
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions) != 0)
        throw DomainError("cannot initialize spawn actions");
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        throw DomainError("cannot initialize spawn attributes");
    }
    const auto cleanup = [&] {
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
    };
    if (posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, writer.value, STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, writer.value, STDERR_FILENO) != 0 ||
        posix_spawn_file_actions_addclose(&actions, reader.value) != 0 ||
        posix_spawn_file_actions_addclose(&actions, writer.value) != 0 ||
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) != 0 ||
        posix_spawnattr_setpgroup(&attributes, 0) != 0) {
        cleanup();
        throw DomainError("cannot configure spawn");
    }
    std::vector<std::string> storage{executable.string()};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    for (auto &arg : storage) {
        if (arg.find('\0') != std::string::npos) {
            cleanup();
            throw DomainError("NUL in process argument");
        }
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    pid_t child = -1;
    const auto error =
        posix_spawnp(&child, executable.c_str(), &actions, &attributes, argv.data(), environ);
    cleanup();
    if (error != 0)
        throw DomainError("cannot start probe executable: " + std::to_string(error));
    close(writer.value);
    writer.value = -1;
    int status = 0;
    bool reaped = false;
    try {
        for (;;) {
            check();
            const auto count = read(reader.value, buffer.data(), buffer.size());
            if (count > 0) {
                append(static_cast<std::size_t>(count));
                continue;
            }
            if (count < 0 && errno != EAGAIN && errno != EINTR)
                throw DomainError("probe read failed");
            if (!reaped) {
                const auto waited = waitpid(child, &status, WNOHANG);
                if (waited < 0 && errno != EINTR)
                    throw DomainError("probe wait failed");
                reaped = waited == child;
            }
            if (reaped && count == 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {
        kill(-child, SIGKILL);
        if (!reaped)
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
        throw;
    }
    kill(-child, SIGKILL); // Do not retain descendants that closed their output handles.
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw DomainError("probe process failed: " + output.substr(0, 512));
#endif
    return output;
}
} // namespace nle::media
