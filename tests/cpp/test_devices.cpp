#include "daemon/ControlServer.hpp"
#include "daemon/DeviceManager.hpp"
#include "daemon/HeadsetDevice.hpp"
#include "daemon/MouseDevice.hpp"
#include "daemon/TelemetrySyncClient.hpp"
#include <gtest/gtest.h>

TEST(MouseDeviceTest, ValidatesDpiAndPollingRateBounds)
{
    ptd::MouseDevice mouse("m1", "Test Mouse", "/dev/hidraw0");
    EXPECT_TRUE(mouse.isConnected());

    const auto ok_res = mouse.applyConfig({{"dpi", 3200}, {"polling_rate_hz", 500}});
    EXPECT_TRUE(ok_res.ok);

    const auto telemetry = std::get<ptd::MouseTelemetry>(mouse.readTelemetry());
    EXPECT_EQ(telemetry.dpi, 3200u);
    EXPECT_EQ(telemetry.polling_rate_hz, 500u);

    const auto bad_dpi = mouse.applyConfig({{"dpi", 99999}});
    EXPECT_FALSE(bad_dpi.ok);

    const auto bad_rate = mouse.applyConfig({{"polling_rate_hz", 333}});
    EXPECT_FALSE(bad_rate.ok);
}

TEST(HeadsetDeviceTest, UpdatesEqPresetAndMuteState)
{
    ptd::HeadsetDevice headset("h1", "Test Headset", "/dev/hidraw1");
    const auto res = headset.applyConfig({{"eq_preset", "FPS"}, {"mic_muted", true}});
    EXPECT_TRUE(res.ok);

    const auto telemetry = std::get<ptd::HeadsetTelemetry>(headset.readTelemetry());
    EXPECT_EQ(telemetry.eq_preset, "FPS");
    EXPECT_TRUE(telemetry.mic_muted);

    EXPECT_FALSE(headset.applyConfig({{"eq_preset", "InvalidPreset"}}).ok);
}

TEST(IntegrationTest, PollsAndPushesTelemetryOverRestApi)
{
    constexpr int kTestPort = 18089;
    ptd::DeviceManager manager(std::chrono::milliseconds(50));
    manager.registerDevice(std::make_shared<ptd::MouseDevice>("mouse-pro-01", "Mouse", "/dev/hidraw0"));

    ptd::TelemetrySyncClient sync_client("127.0.0.1", kTestPort);
    manager.setTelemetryCallback([&sync_client](const nlohmann::json &batch)
                                 { static_cast<void>(sync_client.pushTelemetry(batch)); });

    ptd::ControlServer server(manager, sync_client, "127.0.0.1", kTestPort);
    server.startAsync();

    manager.pollOnce();
    EXPECT_EQ(server.ingestedBatchCount(), 1u);
    EXPECT_EQ(sync_client.metrics().successful_pushes, 1u);

    server.stop();
}