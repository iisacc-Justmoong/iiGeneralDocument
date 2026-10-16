#include "Word/Private/ConversionProcess.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
namespace ii::document::detail {
std::string pathUtf8(const std::filesystem::path &path) {
    auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
std::string localFileUri(const std::filesystem::path &path) {
    const auto raw = std::filesystem::absolute(path).generic_u8string();
    std::string uri = "file://";
    std::size_t start = 0;
#ifdef _WIN32
    if (raw.starts_with(u8"//"))
        start = 2;
    else
        uri += '/';
#endif
    constexpr char digits[] = "0123456789ABCDEF";
    for (auto ch : std::u8string_view(raw).substr(start)) {
        auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~')
            uri += static_cast<char>(c);
        else {
            uri += '%';
            uri += digits[c >> 4];
            uri += digits[c & 15];
        }
    }
    return uri;
}
namespace {
constexpr std::size_t captureLimit = 1024 * 1024;
}
ConversionProcessResult runConversionProcess(const std::filesystem::path &executable,
                                             const std::vector<std::string> &arguments,
                                             const std::filesystem::path &directory,
                                             std::chrono::milliseconds timeout) {
    ConversionProcessResult result;
    if (timeout <= std::chrono::milliseconds::zero()) {
        result.error = "Invalid conversion timeout";
        return result;
    }
#ifdef _WIN32
    struct Handle {
        HANDLE value{};
        ~Handle() {
            if (value && value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
        operator HANDLE() const { return value; }
    };
    auto wide = [](const std::string &s) {
        return std::filesystem::path(
                   std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()))
            .wstring();
    };
    auto quote = [](const std::wstring &s) {
        std::wstring r = L"\"";
        std::size_t slashes = 0;
        for (wchar_t c : s) {
            if (c == L'\\') {
                ++slashes;
                continue;
            }
            r.append(slashes * (c == L'\"' ? 2 : 1), L'\\');
            if (c == L'\"')
                r += L'\\';
            r += c;
            slashes = 0;
        }
        r.append(slashes * 2, L'\\');
        r += L'\"';
        return r;
    };
    std::wstring command = quote(executable.wstring());
    for (const auto &a : arguments)
        command += L" " + quote(wide(a));
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle outRead, outWrite, errRead, errWrite, job;
    if (!CreatePipe(&outRead.value, &outWrite.value, &security, 0) ||
        !CreatePipe(&errRead.value, &errWrite.value, &security, 0)) {
        result.error = "Unable to create conversion pipes";
        return result;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    auto work = directory.wstring();
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, work.c_str(), &startup,
                        &process)) {
        result.error = "Unable to start converter: " + std::to_string(GetLastError());
        return result;
    }
    Handle thread{process.hThread}, child{process.hProcess};
    job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value ||
        !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, child)) {
        TerminateProcess(child, 1);
        WaitForSingleObject(child, 5000);
        result.error = "Unable to isolate converter process";
        return result;
    }
    ResumeThread(thread);
    result.started = true;
    CloseHandle(outWrite.value);
    outWrite.value = nullptr;
    CloseHandle(errWrite.value);
    errWrite.value = nullptr;
    auto drain = [](HANDLE pipe, std::string &text) {
        std::array<char, 4096> buffer;
        DWORD available = 0;
        while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available) {
            DWORD count = 0;
            if (!ReadFile(pipe, buffer.data(), std::min<DWORD>(available, buffer.size()), &count,
                          nullptr) ||
                !count)
                break;
            if (text.size() < captureLimit)
                text.append(buffer.data(),
                            std::min<std::size_t>(count, captureLimit - text.size()));
        }
    };
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (WaitForSingleObject(child, 10) == WAIT_TIMEOUT) {
        drain(outRead, result.output);
        drain(errRead, result.error);
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timedOut = true;
            TerminateJobObject(job, 1);
            WaitForSingleObject(child, 5000);
            break;
        }
    }
    drain(outRead, result.output);
    drain(errRead, result.error);
    DWORD code = 0;
    GetExitCodeProcess(child, &code);
    result.exitCode = static_cast<int>(code);
#else
    struct Descriptor {
        int value{-1};
        ~Descriptor() {
            if (value >= 0)
                ::close(value);
        }
        void close() {
            if (value >= 0)
                ::close(value);
            value = -1;
        }
    };
    int out[2], err[2];
    if (::pipe(out) != 0) {
        result.error = std::strerror(errno);
        return result;
    }
    Descriptor outRead{out[0]}, outWrite{out[1]};
    if (::pipe(err) != 0) {
        result.error = std::strerror(errno);
        return result;
    }
    Descriptor errRead{err[0]}, errWrite{err[1]};
    for (int fd : {out[0], out[1], err[0], err[1]})
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(out[0], F_SETFL, O_NONBLOCK);
    fcntl(err[0], F_SETFL, O_NONBLOCK);
    std::string program = executable.string();
    std::vector<std::string> storage{program};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    for (auto &a : storage)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addclose(&actions, err[0]);
#if defined(__APPLE__)
    int error = 0;
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    if (__builtin_available(macOS 26.0, *))
        error = posix_spawn_file_actions_addchdir(&actions, directory.c_str());
    else
#endif
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        error = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
#pragma clang diagnostic pop
    }
#elif defined(__linux__)
    int error = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
#else
    int error = ENOSYS;
#endif
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    pid_t pid{};
    if (!error)
        error = posix_spawnp(&pid, program.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    outWrite.close();
    errWrite.close();
    if (error) {
        result.error = std::strerror(error);
        return result;
    }
    result.started = true;
    auto drain = [](int pipe, std::string &text) {
        std::array<char, 4096> buffer;
        while (true) {
            auto count = ::read(pipe, buffer.data(), buffer.size());
            if (count <= 0)
                break;
            if (text.size() < captureLimit)
                text.append(buffer.data(), std::min<std::size_t>(static_cast<std::size_t>(count),
                                                                 captureLimit - text.size()));
        }
    };
    auto deadline = std::chrono::steady_clock::now() + timeout;
    int status{};
    while (true) {
        drain(out[0], result.output);
        drain(err[0], result.error);
        auto state = waitpid(pid, &status, WNOHANG);
        if (state == pid)
            break;
        if (state < 0 && errno != EINTR) {
            result.error += " Converter wait failed";
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timedOut = true;
            kill(-pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            break;
        }
        pollfd pipes[2]{{out[0], POLLIN, 0}, {err[0], POLLIN, 0}};
        ::poll(pipes, 2, 20);
    }
    drain(out[0], result.output);
    drain(err[0], result.error);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return result;
}
} // namespace ii::document::detail
