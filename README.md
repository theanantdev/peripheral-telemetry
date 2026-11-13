<!-- @format -->

# Peripheral Telemetry & Device Control Daemon (`ptd`)

A cross-platform **C++20 background service (daemon)** and **REST control plane** designed to simulate low-level hardware peripheral polling, device profile configuration, resilient telemetry synchronization, and automated end-to-end verification.

Inspired by modern peripheral management engines (such as Logitech G HUB and Logi Options+), this project demonstrates production systems-engineering practices: polymorphic hardware abstraction, RAII resource management, lock-safe multithreading, Conan 2.0 + CMake build orchestration, and two-tier automated testing with **GoogleTest** and **Python + Selenium**.

---

## Table of Contents

1. [System Architecture](#system-architecture)
2. [Modern C++20 & Systems Design Highlights](#modern-c20--systems-design-highlights)
3. [Repository Structure](#repository-structure)
4. [Prerequisites](#prerequisites)
5. [Build Instructions (Conan 2.0 + CMake)](#build-instructions-conan-20--cmake)
6. [Running the Daemon & Web Control UI](#running-the-daemon--web-control-ui)
7. [REST API Reference](#rest-api-reference)
8. [Automated Testing (GoogleTest & Selenium)](#automated-testing-googletest--selenium)
9. [Chaos & Fault-Injection Testing](#chaos--fault-injection-testing)

---

## System Architecture

The daemon coordinates four decoupled subsystems across dedicated worker threads:

```text
+-----------------------------------------------------------------------------------+
|                        Peripheral Telemetry Daemon (C++20)                        |
|                                                                                   |
|  +---------------------------+        +----------------------------------------+  |
|  |   DeviceManager           |        |   ControlServer (cpp-httplib)          |  |
|  |   (std::jthread @ 500ms)  |        |   (std::jthread HTTP Listener)         |  |
|  |                           |        |                                        |  |
|  |  - Polls IDevice registry |<------>|  - GET  / (Embedded Control Web UI)    |  |
|  |  - Emits JSON batches     |        |  - GET  /api/v1/devices                |  |
|  +-------------+-------------+        |  - POST /api/v1/devices/:id/config     |  |
|                |                      |  - POST /api/v1/telemetry/ingest       |  |
|                v                      |  - POST /api/v1/fault-mode (503 test)  |  |
|  +---------------------------+        |  - GET  /api/v1/health                 |  |
|  |   Polymorphic Peripherals |        +-------------------+--------------------+  |
|  |   (namespace ptd)         |                            ^                       |
|  |                           |                            | HTTP POST + Backoff   |
|  |  - IDevice (Interface)    |        +-------------------+--------------------+  |
|  |    ├── MouseDevice        |        |   TelemetrySyncClient                  |  |
|  |    └── HeadsetDevice      |------->|   - Connection/Read timeouts           |  |
|  |  - HardwareHandleGuard    |        |   - Exponential retry backoff          |  |
|  +---------------------------+        +----------------------------------------+  |
+-----------------------------------------------------------------------------------+
                                            ^                       ^
                                            |                       |
                            +---------------+---+       +-----------+-----------+
                            | GoogleTest Suite  |       | Pytest + Selenium E2E |
                            | (Unit & API)      |       | (Headless Chrome UI)  |
                            +-------------------+       +-----------------------+
```

### Runtime Execution Flow

1. **Initialization:** `main.cpp` registers simulated peripherals (`MouseDevice` on `/dev/hidraw0` and `HeadsetDevice` on `/dev/hidraw1`) into `DeviceManager`, wires the telemetry callback to `TelemetrySyncClient`, and starts `ControlServer` on `127.0.0.1:8080`.
2. **Background Polling:** `DeviceManager` spawns a C++20 `std::jthread` that polls all registered devices every $500\text{ ms}$, aggregates device state into a JSON payload, and triggers the upstream sync callback.
3. **Resilient Telemetry Sync:** `TelemetrySyncClient` posts batches to `/api/v1/telemetry/ingest` with bounded timeouts and exponential retry backoff ($50\text{ ms} \rightarrow 100\text{ ms} \rightarrow 200\text{ ms}$), tracking atomic delivery metrics.
4. **Live Configuration & Inspection:** Users (or automated Selenium scripts) interact with the web dashboard or REST endpoints to inspect telemetry or update hardware parameters (such as Mouse DPI or Polling Rate) with strict schema and bounds validation.
5. **Graceful Shutdown:** Sending `SIGINT` or `SIGTERM` (`Ctrl+C`) signals `std::stop_token`, cleanly joining all worker threads and invoking RAII destructors to close simulated HID handles.

---

## Modern C++20 & Systems Design Highlights

- **Polymorphic Device Abstraction (`IDevice`):** Decouples the device manager and HTTP transport layer from peripheral-specific implementations (`MouseDevice`, `HeadsetDevice`), allowing new hardware types to be added without modifying server or polling logic.
- **Deterministic RAII Resource Management (`HardwareHandleGuard`):** Encapsulates simulated OS HID/USB file descriptors, guaranteeing handle release upon scope exit or stack unwinding while enforcing move-only semantics.
- **Cooperative Multithreading (`std::jthread` & `std::stop_token`):** Eliminates manual thread-joining boilerplate and ensures responsive, leak-free shutdown without race conditions.
- **Fine-Grained Concurrency Control:**
  - `std::shared_mutex` in `DeviceManager` allows concurrent multi-threaded reads (`snapshotAll()`) while serializing device registration.
  - Per-device `std::mutex` instances prevent lock contention across independent peripherals and avoid nested lock inversions during polling and HTTP configuration updates.
- **Type-Safe Modern C++ Vocabulary:** Uses `std::variant` (`TelemetryPayload`) for heterogeneous sensor payloads, `std::string_view` for zero-copy path access, and `[[nodiscard]]` attributes to prevent ignored error/status values.
- **Cross-Platform Namespace Isolation (`namespace ptd`):** Uses `ptd` (Peripheral Telemetry Daemon) rather than `daemon` to avoid symbol collisions with the legacy POSIX `int daemon(int, int)` function declared in macOS/BSD `<stdlib.h>`.
- **Strict Compiler Hygiene:** Compiled with `-std=c++20 -Wall -Wextra -Wpedantic -Werror` (or `/W4 /WX` on MSVC).

---

## Repository Structure

```text
peripheral-telemetry-daemon/
├── CMakeLists.txt                  # Multi-target CMake build (core lib, daemon exe, unit tests)
├── conanfile.txt                   # Conan 2.0 dependency manifest & CMake generators
├── README.md                       # Project documentation
├── include/
│   └── daemon/
│       ├── IDevice.hpp             # IDevice interface, HardwareHandleGuard RAII, TelemetryPayload
│       ├── MouseDevice.hpp         # Mouse peripheral interface (DPI, polling rate, battery)
│       ├── HeadsetDevice.hpp       # Headset peripheral interface (EQ preset, mic mute, thermal)
│       ├── DeviceManager.hpp       # Thread-safe device registry & std::jthread polling loop
│       ├── TelemetrySyncClient.hpp # HTTP REST client with timeouts & exponential backoff
│       └── ControlServer.hpp       # Embedded cpp-httplib REST server & HTML/JS dashboard
├── src/
│   ├── MouseDevice.cpp
│   ├── HeadsetDevice.cpp
│   ├── DeviceManager.cpp
│   ├── TelemetrySyncClient.cpp
│   ├── ControlServer.cpp
│   └── main.cpp                    # Signal handling & daemon lifecycle orchestration
└── tests/
    ├── cpp/
    │   └── test_devices.cpp        # GoogleTest unit & HTTP integration tests
    └── e2e/
        ├── requirements.txt        # Pytest, Selenium, and Requests dependencies
        └── test_ui_and_api.py      # Headless Chrome end-to-end UI & fault-injection tests
```

---

## Prerequisites

- **C++ Compiler:** Clang 15+ (AppleClang 15+), GCC 11+, or MSVC 2022 with C++20 support
- **Build Tools:** CMake $\ge 3.20$ and Conan $2.x$
- **Python & Browser (for E2E tests):** Python $3.10+$ and Google Chrome (Selenium Manager automatically resolves the matching ChromeDriver binary)

---

## Build Instructions (Conan 2.0 + CMake)

### 1. Install Dependencies via Conan 2.0

Conan resolves and builds `nlohmann_json/3.11.3`, `cpp-httplib/0.15.3`, and `gtest/1.14.0`, generating `conan_toolchain.cmake`:

```bash
pip install conan
conan profile detect --force
conan install . --output-folder=build --build=missing
```

### 2. Configure & Compile with CMake

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j
```

This produces two binaries:

- `build/peripheral_daemon` — Main background service executable
- `build/unit_tests` — C++ GoogleTest runner

---

## Running the Daemon & Web Control UI

Start the daemon on port `8080` (or pass any custom port as the first CLI argument):

```bash
./build/peripheral_daemon 8080
```

Open **`http://127.0.0.1:8080`** in your browser to access the **Peripheral Telemetry & Control Dashboard**, where you can:

- Adjust **Sensor DPI** ($100\text{–}26000$) and **Polling Rate** ($125, 250, 500, 1000\text{ Hz}$) for `mouse-pro-01`
- View live JSON telemetry refreshing every second
- Toggle **503 Fault Mode** to test error propagation in real time

Press `Ctrl+C` in the terminal to trigger a graceful shutdown.

---

## REST API Reference

| Method | Endpoint                      | Description                                                                      |
| :----- | :---------------------------- | :------------------------------------------------------------------------------- |
| `GET`  | `/`                           | Serves the embedded HTML/JS device configuration dashboard.                      |
| `GET`  | `/api/v1/health`              | Returns service health, fault-mode state, and atomic sync/retry metrics.         |
| `GET`  | `/api/v1/devices`             | Returns a JSON snapshot of all connected peripherals and their live telemetry.   |
| `POST` | `/api/v1/devices/{id}/config` | Validates and applies a JSON configuration patch to a specific device.           |
| `POST` | `/api/v1/telemetry/ingest`    | Receives batched telemetry payloads pushed by `TelemetrySyncClient`.             |
| `POST` | `/api/v1/fault-mode`          | Enables or disables simulated `HTTP 503` backend failures (`{"enabled": true}`). |

### Example `curl` Commands

**Inspect all devices:**

```bash
curl -s http://127.0.0.1:8080/api/v1/devices | python3 -m json.tool
```

**Update Mouse DPI and Polling Rate:**

```bash
curl -X POST http://127.0.0.1:8080/api/v1/devices/mouse-pro-01/config \
  -H "Content-Type: application/json" \
  -d '{"dpi": 3200, "polling_rate_hz": 1000}'
```

**Update Headset EQ Preset and Mic Mute State:**

```bash
curl -X POST http://127.0.0.1:8080/api/v1/devices/headset-pro-02/config \
  -H "Content-Type: application/json" \
  -d '{"eq_preset": "FPS", "mic_muted": true}'
```

**Check Telemetry Sync & Retry Metrics:**

```bash
curl -s http://127.0.0.1:8080/api/v1/health | python3 -m json.tool
```

---

## Automated Testing (GoogleTest & Selenium)

### 1. C++ Unit & Integration Tests (`CTest` / `GoogleTest`)

Validates device bounds checking, `std::variant` telemetry reads, and end-to-end polling + HTTP ingestion:

```bash
ctest --test-dir build --output-on-failure
```

### 2. Python + Selenium End-to-End Tests (`Pytest`)

Automatically launches `build/peripheral_daemon` on an isolated test port (`18085`), drives a headless Chrome instance using explicit `WebDriverWait` conditions, and verifies both DOM updates and C++ backend state persistence:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r tests/e2e/requirements.txt
pytest tests/e2e/test_ui_and_api.py -v
```

---

## Chaos & Fault-Injection Testing

To verify resilience under hardware bus timeouts or upstream outages without modifying source code:

1. Send `POST /api/v1/fault-mode` with `{"enabled": true}` (or click **Toggle 503 Fault Mode** in the UI).
2. All device polling/configuration endpoints return `HTTP 503 Service Unavailable`.
3. `TelemetrySyncClient` automatically retries failed pushes using exponential backoff and increments `retry_attempts` and `failed_pushes` counters, visible via `GET /api/v1/health`.
4. Send `{"enabled": false}` to restore normal operation.
