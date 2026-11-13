#include "daemon/HeadsetDevice.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace ptd
{

    HeadsetDevice::HeadsetDevice(std::string id, std::string name, std::string hid_path)
        : id_(std::move(id)), name_(std::move(name)), handle_(std::move(hid_path)) {}

    std::string HeadsetDevice::id() const { return id_; }
    std::string HeadsetDevice::name() const { return name_; }
    bool HeadsetDevice::isConnected() const noexcept { return handle_.isOpen(); }

    void HeadsetDevice::pollHardware()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!handle_.isOpen())
        {
            return;
        }
        ++poll_counter_;
        state_.internal_temp_c = 31.5 + 0.4 * std::sin(static_cast<double>(poll_counter_) * 0.25);
    }

    TelemetryPayload HeadsetDevice::readTelemetry() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    ConfigResult HeadsetDevice::applyConfig(const nlohmann::json &config_patch)
    {
        if (!config_patch.is_object())
        {
            return ConfigResult::failure("Configuration payload must be a JSON object.");
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (config_patch.contains("eq_preset"))
        {
            if (!config_patch["eq_preset"].is_string())
            {
                return ConfigResult::failure("Field 'eq_preset' must be a string.");
            }
            const auto preset = config_patch["eq_preset"].get<std::string>();
            constexpr std::array<std::string_view, 4> kValidPresets{"Flat", "BassBoost", "FPS", "Podcast"};
            if (std::find(kValidPresets.begin(), kValidPresets.end(), preset) == kValidPresets.end())
            {
                return ConfigResult::failure("eq_preset must be one of: Flat, BassBoost, FPS, Podcast.");
            }
            state_.eq_preset = preset;
        }

        if (config_patch.contains("mic_muted"))
        {
            if (!config_patch["mic_muted"].is_boolean())
            {
                return ConfigResult::failure("Field 'mic_muted' must be a boolean.");
            }
            state_.mic_muted = config_patch["mic_muted"].get<bool>();
        }

        return ConfigResult::success();
    }

    nlohmann::json HeadsetDevice::toJson() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nlohmann::json{
            {"id", id_},
            {"name", name_},
            {"type", type()},
            {"connected", handle_.isOpen()},
            {"telemetry", {{"eq_preset", state_.eq_preset}, {"mic_muted", state_.mic_muted}, {"battery_percent", state_.battery_percent}, {"internal_temp_c", state_.internal_temp_c}}}};
    }

}