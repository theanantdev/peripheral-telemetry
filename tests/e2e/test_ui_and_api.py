import subprocess
import time
from pathlib import Path

import pytest
import requests
from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import Select, WebDriverWait

PORT = 18085
BASE_URL = f"http://127.0.0.1:{PORT}"
BINARY_PATH = Path(__file__).resolve().parents[2] / "build" / "peripheral_daemon"


@pytest.fixture(scope="module")
def daemon_process():
    if not BINARY_PATH.exists():
        pytest.fail(
            f"Daemon binary not found at {BINARY_PATH}. Build C++ project first."
        )

    proc = subprocess.Popen([str(BINARY_PATH), str(PORT)])
    for _ in range(30):
        try:
            r = requests.get(f"{BASE_URL}/api/v1/health", timeout=0.5)
            if r.status_code == 200:
                break
        except requests.RequestException:
            time.sleep(0.1)
    else:
        proc.terminate()
        pytest.fail("Daemon failed to start within timeout.")

    yield proc
    proc.terminate()
    proc.wait(timeout=5)


@pytest.fixture
def driver(daemon_process):
    requests.post(f"{BASE_URL}/api/v1/fault-mode", json={"enabled": False}, timeout=2)
    options = Options()
    options.add_argument("--headless=new")
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-dev-shm-usage")
    browser = webdriver.Chrome(options=options)
    yield browser
    browser.quit()


def test_update_mouse_config_via_ui_persists_to_cpp_backend(driver):
    driver.get(BASE_URL)
    wait = WebDriverWait(driver, 5)

    dpi_input = wait.until(EC.presence_of_element_located((By.ID, "dpi-input")))
    dpi_input.clear()
    dpi_input.send_keys("6400")

    polling_select = Select(driver.find_element(By.ID, "polling-select"))
    polling_select.select_by_value("500")

    driver.find_element(By.ID, "apply-mouse-btn").click()

    banner = wait.until(EC.visibility_of_element_located((By.ID, "status-banner")))
    assert "Configuration applied successfully" in banner.text

    devices = requests.get(f"{BASE_URL}/api/v1/devices", timeout=2).json()["devices"]
    mouse = next(d for d in devices if d["id"] == "mouse-pro-01")
    assert mouse["telemetry"]["dpi"] == 6400
    assert mouse["telemetry"]["polling_rate_hz"] == 500


def test_invalid_dpi_shows_validation_error(driver):
    driver.get(BASE_URL)
    wait = WebDriverWait(driver, 5)

    dpi_input = wait.until(EC.presence_of_element_located((By.ID, "dpi-input")))
    dpi_input.clear()
    dpi_input.send_keys("99999")
    driver.find_element(By.ID, "apply-mouse-btn").click()

    banner = wait.until(EC.visibility_of_element_located((By.ID, "status-banner")))
    assert "DPI must be between 100 and 26000" in banner.text


def test_fault_mode_surfaces_503_error_in_ui(driver):
    driver.get(BASE_URL)
    wait = WebDriverWait(driver, 5)

    wait.until(EC.element_to_be_clickable((By.ID, "toggle-fault-btn"))).click()
    wait.until(EC.element_to_be_clickable((By.ID, "apply-mouse-btn"))).click()

    banner = wait.until(EC.visibility_of_element_located((By.ID, "status-banner")))
    assert "503" in banner.text or "fault mode" in banner.text.lower()
