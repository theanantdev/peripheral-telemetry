#include "daemon/ControlServer.hpp"
#include "daemon/DeviceManager.hpp"
#include "daemon/HeadsetDevice.hpp"
#include "daemon/MouseDevice.hpp"
#include "daemon/TelemetrySyncClient.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace
{
    std::atomic<bool> g_running{true};

    void handleSignal(int)
    {
        g_running.store(false);
    }
} // namespace

int main(int argc, char *argv[])
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const int port = (argc > 1) ? std::atoi(argv[1]) : 8080;

    ptd::DeviceManager manager(std::chrono::milliseconds(500));
    manager.registerDevice(std::make_shared<ptd::MouseDevice>(
        "mouse-pro-01", "Pro X Superlight Sim", "/dev/hidraw0"));
    manager.registerDevice(std::make_shared<ptd::HeadsetDevice>(
        "headset-pro-02", "Pro X Wireless Headset Sim", "/dev/hidraw1"));

    ptd::TelemetrySyncClient sync_client("127.0.0.1", port);
    manager.setTelemetryCallback([&sync_client](const nlohmann::json &telemetry_batch)
                                 { static_cast<void>(sync_client.pushTelemetry(telemetry_batch)); });

    ptd::ControlServer server(manager, sync_client, "127.0.0.1", port);
    server.startAsync();
    manager.startPolling();

    std::cout << "Peripheral Telemetry Daemon listening on http://127.0.0.1:" << port << "\n";
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Shutting down daemon cleanly...\n";
    manager.stopPolling();
    server.stop();
    return 0;
}