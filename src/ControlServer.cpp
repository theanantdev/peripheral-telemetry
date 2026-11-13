#include "daemon/ControlServer.hpp"
#include <chrono>
#include <thread>

namespace ptd
{

    ControlServer::ControlServer(DeviceManager &manager,
                                 TelemetrySyncClient &sync_client,
                                 std::string bind_host,
                                 int port)
        : manager_(manager),
          sync_client_(sync_client),
          bind_host_(std::move(bind_host)),
          port_(port)
    {
        setupRoutes();
    }

    ControlServer::~ControlServer()
    {
        stop();
    }

    void ControlServer::setupRoutes()
    {
        server_.Get("/", [](const httplib::Request &, httplib::Response &res)
                    { res.set_content(renderDashboardHtml(), "text/html; charset=utf-8"); });

        server_.Get("/api/v1/health", [this](const httplib::Request &, httplib::Response &res)
                    {
        const auto m = sync_client_.metrics();
        nlohmann::json out{
            {"status", "ok"},
            {"fault_mode", fault_mode_enabled_.load()},
            {"sync_metrics", {
                {"successful_pushes", m.successful_pushes},
                {"failed_pushes", m.failed_pushes},
                {"retry_attempts", m.retry_attempts}
            }}
        };
        res.set_content(out.dump(), "application/json"); });

        server_.Get("/api/v1/devices", [this](const httplib::Request &, httplib::Response &res)
                    {
            if (fault_mode_enabled_.load())
            {
                res.status = 503;
                res.set_content(R"({"error":"Backend fault mode active: unable to fetch devices"})",
                                "application/json");
            return;
        }
        nlohmann::json out{{"devices", manager_.snapshotAll()}};
        res.set_content(out.dump(), "application/json"); });

        server_.Post(R"(/api/v1/devices/([a-zA-Z0-9_-]+)/config)",
                     [this](const httplib::Request &req, httplib::Response &res)
                     {
                         if (fault_mode_enabled_.load())
                         {
                             res.status = 503;
                             res.set_content(R"({"error":"Backend fault mode active: unable to commit config"})",
                                             "application/json");
                             return;
                         }

                         const std::string device_id = req.matches[1].str();
                         auto device = manager_.getDevice(device_id);
                         if (!device)
                         {
                             res.status = 404;
                             res.set_content(R"({"error":"Device not found"})", "application/json");
                             return;
                         }

                         nlohmann::json parsed;
                         try
                         {
                             parsed = nlohmann::json::parse(req.body);
                         }
                         catch (const nlohmann::json::parse_error &)
                         {
                             res.status = 400;
                             res.set_content(R"({"error":"Malformed JSON request body"})", "application/json");
                             return;
                         }

                         const auto result = device->applyConfig(parsed);
                         if (!result.ok)
                         {
                             res.status = 400;
                             nlohmann::json err{{"error", result.error_message}};
                             res.set_content(err.dump(), "application/json");
                             return;
                         }

                         res.status = 200;
                         res.set_content(device->toJson().dump(), "application/json");
                     });

        server_.Post("/api/v1/fault-mode", [this](const httplib::Request &req, httplib::Response &res)
                     {
        try {
            const auto body = nlohmann::json::parse(req.body);
            if (!body.contains("enabled") || !body["enabled"].is_boolean()) {
                res.status = 400;
                res.set_content(R"({"error":"Boolean field 'enabled' is required"})", "application/json");
                return;
            }
            fault_mode_enabled_.store(body["enabled"].get<bool>());
            res.set_content(nlohmann::json{{"fault_mode", fault_mode_enabled_.load()}}.dump(),
                            "application/json");
        } catch (const nlohmann::json::parse_error&) {
            res.status = 400;
            res.set_content(R"({"error":"Malformed JSON"})", "application/json");
        } });

        server_.Post("/api/v1/telemetry/ingest", [this](const httplib::Request &req, httplib::Response &res)
                     {
        if (fault_mode_enabled_.load()) {
            res.status = 503;
            res.set_content(R"({"error":"Telemetry ingestion temporarily unavailable"})", "application/json");
            return;
        }
        try {
            auto payload = nlohmann::json::parse(req.body);
            if (!payload.contains("devices") || !payload["devices"].is_array()) {
                res.status = 400;
                res.set_content(R"({"error":"Schema error: 'devices' array required"})", "application/json");
                return;
            }
            {
                std::lock_guard<std::mutex> lock(ingest_mutex_);
                ingested_batches_.push_back(std::move(payload));
            }
            res.status = 202;
            res.set_content(R"({"status":"accepted"})", "application/json");
        } catch (const nlohmann::json::parse_error&) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
        } });
    }

    void ControlServer::startAsync()
    {
        if (server_thread_.joinable())
        {
            return;
        }
        server_thread_ = std::jthread([this](std::stop_token)
                                      { server_.listen(bind_host_, port_); });
        while (!server_.is_running())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void ControlServer::stop()
    {
        if (server_.is_running())
        {
            server_.stop();
        }
        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
    }

    size_t ControlServer::ingestedBatchCount() const
    {
        std::lock_guard<std::mutex> lock(ingest_mutex_);
        return ingested_batches_.size();
    }

    std::string ControlServer::renderDashboardHtml()
    {
        return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>Peripheral Telemetry & Device Control</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; max-width: 820px; margin: 2rem auto; padding: 0 1rem; background: #0f172a; color: #f8fafc; }
    .card { background: #1e293b; border-radius: 8px; padding: 1.25rem; margin-bottom: 1rem; border: 1px solid #334155; }
    label { display: block; margin-top: 0.75rem; font-size: 0.9rem; color: #cbd5e1; }
    input, select, button { margin-top: 0.35rem; padding: 0.5rem 0.75rem; border-radius: 6px; border: 1px solid #475569; background: #0f172a; color: #f8fafc; }
    button { background: #2563eb; border: none; cursor: pointer; font-weight: 600; margin-top: 1rem; }
    button.danger { background: #dc2626; margin-left: 0.5rem; }
    #status-banner { padding: 0.75rem; border-radius: 6px; margin-bottom: 1rem; display: none; }
    .ok { background: #065f46; color: #ecfdf5; display: block !important; }
    .err { background: #991b1b; color: #fef2f2; display: block !important; }
    pre { background: #020617; padding: 0.75rem; border-radius: 6px; overflow-x: auto; }
  </style>
</head>
<body>
  <h1>Peripheral Telemetry & Control Daemon</h1>
  <div id="status-banner" role="alert"></div>

  <div class="card">
    <h2>Mouse Configuration (mouse-pro-01)</h2>
    <label for="dpi-input">Sensor DPI (100 - 26000)</label>
    <input id="dpi-input" type="number" value="1600" />

    <label for="polling-select">Polling Rate (Hz)</label>
    <select id="polling-select">
      <option value="125">125 Hz</option>
      <option value="250">250 Hz</option>
      <option value="500">500 Hz</option>
      <option value="1000" selected>1000 Hz</option>
    </select>

    <div>
      <button id="apply-mouse-btn" onclick="applyMouseConfig()">Apply Mouse Config</button>
      <button id="toggle-fault-btn" class="danger" onclick="toggleFaultMode()">Toggle 503 Fault Mode</button>
    </div>
  </div>

  <div class="card">
    <h2>Live Device Telemetry</h2>
    <pre id="telemetry-output">Loading telemetry...</pre>
  </div>

  <script>
    let faultEnabled = false;

    function showBanner(msg, isError) {
      const el = document.getElementById('status-banner');
      el.textContent = msg;
      el.className = isError ? 'err' : 'ok';
    }

    async function refreshTelemetry() {
      try {
        const res = await fetch('/api/v1/devices');
        const data = await res.json();
        if (!res.ok) {
          showBanner(data.error || 'Failed to fetch telemetry', true);
          return;
        }
        document.getElementById('telemetry-output').textContent = JSON.stringify(data, null, 2);
      } catch (e) {
        showBanner('Network error communicating with daemon', true);
      }
    }

    async function applyMouseConfig() {
      const dpi = Number(document.getElementById('dpi-input').value);
      const polling = Number(document.getElementById('polling-select').value);
      const res = await fetch('/api/v1/devices/mouse-pro-01/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ dpi: dpi, polling_rate_hz: polling })
      });
      const data = await res.json();
      if (!res.ok) {
        showBanner('Error: ' + (data.error || res.status), true);
        return;
      }
      showBanner('Configuration applied successfully', false);
      await refreshTelemetry();
    }

    async function toggleFaultMode() {
      faultEnabled = !faultEnabled;
      const res = await fetch('/api/v1/fault-mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: faultEnabled })
      });
      const data = await res.json();
      showBanner('Fault mode set to: ' + data.fault_mode, data.fault_mode);
    }

    refreshTelemetry();
    setInterval(refreshTelemetry, 1000);
  </script>
</body>
</html>)HTML";
    }

}