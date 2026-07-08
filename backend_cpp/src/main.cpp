#include <csignal>

#include "app/ApplicationRuntime.hpp"

namespace {
// Signal handler 不能直接捕获局部对象，因此保留一个原始指针，
// 在这里仅发出协作式停止请求。
ApplicationRuntime* g_app{nullptr};

void handleSignal(int) {
    if (g_app != nullptr) {
        g_app->requestStop();
    }
}
} // namespace

int main() {
    // main 只负责进程级生命周期编排。
    ApplicationRuntime app;
    g_app = &app;
    std::signal(SIGINT, handleSignal);

    const bool started = app.start();
    if (!started) {
        g_app = nullptr;
        return 1;
    }

    app.runUntilStopped();
    app.stop();
    g_app = nullptr;
    return 0;
}