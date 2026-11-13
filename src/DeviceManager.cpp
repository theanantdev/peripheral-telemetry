#include "daemon/DeviceManager.hpp"

namespace ptd
{

    DeviceManager::DeviceManager(std::chrono::milliseconds poll_interval)
        : poll_interval_(poll_interval) {}

    DeviceManager::~DeviceManager()
    {
        stopPolling();
    }

    void DeviceManager::registerDevice(std::shared_ptr<IDevice> device)
    {
        if (!device)
        {
            return;
        }
        std::unique_lock<std::shared_mutex> lock(registry_mutex_);
        devices_[device->id()] = std::move(device);
    }

    std::shared_ptr<IDevice> DeviceManager::getDevice(const std::string &id) const
    {
        std::shared_lock<std::shared_mutex> lock(registry_mutex_);
        const auto it = devices_.find(id);
        return (it != devices_.end()) ? it->second : nullptr;
    }

    std::vector<nlohmann::json> DeviceManager::snapshotAll() const
    {
        std::shared_lock<std::shared_mutex> lock(registry_mutex_);
        std::vector<nlohmann::json> result;
        result.reserve(devices_.size());
        for (const auto &[_, device] : devices_)
        {
            result.push_back(device->toJson());
        }
        return result;
    }

    void DeviceManager::setTelemetryCallback(TelemetryCallback cb)
    {
        std::unique_lock<std::shared_mutex> lock(registry_mutex_);
        on_telemetry_ = std::move(cb);
    }

    void DeviceManager::pollOnce()
    {
        std::vector<std::shared_ptr<IDevice>> snapshot_devices;
        TelemetryCallback callback_copy;

        {
            std::shared_lock<std::shared_mutex> lock(registry_mutex_);
            snapshot_devices.reserve(devices_.size());
            for (const auto &[_, dev] : devices_)
            {
                snapshot_devices.push_back(dev);
            }
            callback_copy = on_telemetry_;
        }

        nlohmann::json batch = nlohmann::json::array();
        for (const auto &dev : snapshot_devices)
        {
            dev->pollHardware();
            batch.push_back(dev->toJson());
        }

        if (callback_copy && !batch.empty())
        {
            callback_copy(nlohmann::json{{"devices", std::move(batch)}});
        }
    }

    void DeviceManager::startPolling()
    {
        if (poll_thread_.joinable())
        {
            return;
        }
        poll_thread_ = std::jthread([this](std::stop_token stoken)
                                    {
        while (!stoken.stop_requested()) {
            pollOnce();
            std::this_thread::sleep_for(poll_interval_);
        } });
    }

    void DeviceManager::stopPolling()
    {
        if (poll_thread_.joinable())
        {
            poll_thread_.request_stop();
            poll_thread_.join();
        }
    }

}