#include "Word/Private/ConversionProcess.h"
#include "TestSupport.h"
#include <future>
#include <iiFileProviderNative.h>
#include <thread>
using namespace ii::document::detail;
int main(int argc, char **argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--child") {
        if (argc > 2 && std::string_view(argv[2]) == "wait")
            std::this_thread::sleep_for(std::chrono::seconds(2));
        if (argc > 2 && std::string_view(argv[2]) == "brief")
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (argc > 3)
            std::cout << argv[3];
        std::cerr << "isolated stderr";
        return 0;
    }
    const auto executable = std::filesystem::absolute(argv[0]);
    iiFileProvider::native::TemporaryDirectory directory;
    const std::string text = "한국어 😀 with spaces and \"quotes\" and backslash \\";
    auto result = runConversionProcess(executable, {"--child", "echo", text}, directory.path(),
                                       std::chrono::seconds(3));
    expect(result.started && !result.timedOut && result.exitCode == 0,
           "native process starts and exits normally");
    expect(result.output == text && result.error == "isolated stderr",
           "native arguments and separate output channels are exact");
    auto independent = std::async(std::launch::async, [&] {
        return runConversionProcess(executable, {"--child", "brief", "independent"},
                                    directory.path(), std::chrono::seconds(3));
    });
    auto timed = runConversionProcess(executable, {"--child", "wait", "timeout"}, directory.path(),
                                      std::chrono::milliseconds(50));
    expect(timed.started && timed.timedOut, "converter deadline terminates its own process");
    auto preserved = independent.get();
    expect(preserved.started && !preserved.timedOut && preserved.exitCode == 0 &&
               preserved.output == "independent",
           "one converter timeout preserves another process");
    auto missing = runConversionProcess(directory.path() / "missing", {}, directory.path(),
                                        std::chrono::seconds(1));
    expect(!missing.started, "missing converter reports a start failure");
    auto invalid =
        runConversionProcess(executable, {}, directory.path(), std::chrono::milliseconds(0));
    expect(!invalid.started, "invalid converter timeout is rejected");
    return 0;
}
