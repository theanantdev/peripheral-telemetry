#pragma once

#include "daemon/IDevice.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ptd
{

    class DeviceManager
    {
    public:
        using TelemetryCallback = std::function<void(const nlohmann::json &)>;

        explicit DeviceManager(std::chrono::milliseconds poll_interval = std::chrono::milliseconds(500));
        ~DeviceManager();

        DeviceManager(const DeviceManager &) = delete;
        DeviceManager &operator=(const DeviceManager &) = delete;

        void registerDevice(std::shared_ptr<IDevice> device);
        [[nodiscard]] std::shared_ptr<IDevice> getDevice(const std::string &id) const;
        [[nodiscard]] std::vector<nlohmann::json> snapshotAll() const;

        void setTelemetryCallback(TelemetryCallback cb);
        void startPolling();
        void stopPolling();
        void pollOnce();

    private:
        std::chrono::milliseconds poll_interval_;
        mutable std::shared_mutex registry_mutex_;
        std::unordered_map<std::string, std::shared_ptr<IDevice>> devices_;
        TelemetryCallback on_telemetry_;
        std::jthread poll_thread_;
    };

}