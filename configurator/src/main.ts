const statusEl = document.querySelector<HTMLParagraphElement>("#status");
const connectBtn = document.querySelector<HTMLButtonElement>("#connect-ble");

function setStatus(text: string): void {
  if (statusEl) {
    statusEl.textContent = text;
  }
}

async function connectViaWebBluetooth(): Promise<void> {
  if (!("bluetooth" in navigator)) {
    setStatus("Web Bluetooth is not available in this browser.");
    return;
  }

  try {
    const device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: false,
      optionalServices: ["battery_service"],
      filters: [{ namePrefix: "VizKey" }]
    });
    setStatus(`Selected device: ${device.name ?? "Unknown"}`);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    setStatus(`BLE connection canceled/failed: ${message}`);
  }
}

if (connectBtn) {
  connectBtn.addEventListener("click", () => {
    void connectViaWebBluetooth();
  });
}
