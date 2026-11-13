#include "daemon/MouseDevice.hpp"
#include <algorithm>
#include <array>

namespace ptd
{

    MouseDevice::MouseDevice(std::string id, std::string name, std::string hid_path)
        : id_(std::move(id)), name_(std::move(name)), handle_(std::move(hid_path)) {}

    std::string MouseDevice::id() const { return id_; }
    std::string MouseDevice::name() const { return name_; }
    bool MouseDevice::isConnected() const noexcept { return handle_.isOpen(); }

    void MouseDevice::pollHardware()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!handle_.isOpen())
        {
            return;
        }
        ++poll_counter_;
        if (poll_counter_ % 20 == 0 && state_.battery_percent > 5)
        {
            --state_.battery_percent;
        }
    }

    TelemetryPayload MouseDevice::readTelemetry() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    ConfigResult MouseDevice::applyConfig(const nlohmann::json &config_patch)
    {
        if (!config_patch.is_object())
        {
            return ConfigResult::failure("Configuration payload must be a JSON object.");
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (config_patch.contains("dpi"))
        {
            if (!config_patch["dpi"].is_number_unsigned() && !config_patch["dpi"].is_number_integer())
            {
                return ConfigResult::failure("Field 'dpi' must be an integer.");
            }
            const int64_t requested_dpi = config_patch["dpi"].get<int64_t>();
            if (requested_dpi < 100 || requested_dpi > 26000)
            {
                return ConfigResult::failure("DPI must be between 100 and 26000.");
            }
            state_.dpi = static_cast<uint32_t>(requested_dpi);
        }

        if (config_patch.contains("polling_rate_hz"))
        {
            if (!config_patch["polling_rate_hz"].is_number_integer())
            {
                return ConfigResult::failure("Field 'polling_rate_hz' must be an integer.");
            }
            const int64_t hz = config_patch["polling_rate_hz"].get<int64_t>();
            constexpr std::array<int64_t, 4> kValidRates{125, 250, 500, 1000};
            if (std::find(kValidRates.begin(), kValidRates.end(), hz) == kValidRates.end())
            {
                return ConfigResult::failure("polling_rate_hz must be one of: 125, 250, 500, 1000.");
            }
            state_.polling_rate_hz = static_cast<uint32_t>(hz);
        }

        return ConfigResult::success();
    }

    nlohmann::json MouseDevice::toJson() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nlohmann::json{
            {"id", id_},
            {"name", name_},
            {"type", type()},
            {"connected", handle_.isOpen()},
            {"telemetry", {{"dpi", state_.dpi}, {"polling_rate_hz", state_.polling_rate_hz}, {"battery_percent", state_.battery_percent}, {"surface_lift_mm", state_.surface_lift_mm}}}};
    }

}