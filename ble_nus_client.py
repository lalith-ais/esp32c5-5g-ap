#!/usr/bin/env python3
"""
ble_nus_client.py  –  BLE NUS client for iMX6 (Debian)

Connects to the ESP32-C5 BLE NUS server and controls the WiFi AP channel
via MQTT messages on topic /VAL200/channel.

MQTT message payload:
    A channel number as a plain string, e.g. "36" or "6"
    The client sends "ch <n>" to the ESP32-C5 over BLE NUS.

Responses from the ESP32-C5 are published back to /VAL200/channel/response.

Dependencies (all from Debian packages):
    sudo apt install python3-bleak python3-paho-mqtt

Usage:
    python3 ble_nus_client.py                    # auto-scan for ESP32C5_NUS
    python3 ble_nus_client.py XX:XX:XX:XX:XX:XX  # connect by MAC address
"""

import asyncio
import sys
import logging
import paho.mqtt.client as mqtt

from bleak import BleakClient, BleakScanner

# ── BLE NUS UUIDs ─────────────────────────────────────────────────────
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # write to ESP32
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # notify from ESP32
DEVICE_NAME = "ESP32C5_NUS"

# ── MQTT config ───────────────────────────────────────────────────────
MQTT_BROKER   = "192.168.99.1"
MQTT_PORT     = 1883
MQTT_SUB      = "/VAL200/channel"           # listen for channel commands
MQTT_PUB      = "/VAL200/channel/response"  # publish ESP32-C5 responses

# ── Logging ───────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-7s  %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("ble_nus")

# ── Shared state between MQTT callbacks and BLE coroutine ─────────────
# asyncio queue bridges the MQTT thread and the BLE event loop
_cmd_queue: asyncio.Queue = None
_ble_client: BleakClient  = None
_mqtt_client: mqtt.Client = None
_loop: asyncio.AbstractEventLoop = None


# ─────────────────────────────────────────────────────────────────────
#  BLE helpers
# ─────────────────────────────────────────────────────────────────────

def ble_rx_handler(sender, data: bytearray):
    """Called on BLE NUS TX notification (response from ESP32-C5)."""
    text = data.decode(errors="replace").strip()
    if text:
        log.info("ESP32-C5 → %s", text)
        if _mqtt_client and _mqtt_client.is_connected():
            _mqtt_client.publish(MQTT_PUB, text)


async def find_device(mac: str = None) -> str:
    if mac:
        return mac
    log.info("Scanning for '%s'...", DEVICE_NAME)
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
    if device is None:
        log.error("'%s' not found. Is the ESP32-C5 powered on?", DEVICE_NAME)
        sys.exit(1)
    log.info("Found: %s  [%s]", device.name, device.address)
    return device.address


async def ble_send(cmd: str):
    """Send a command string to the ESP32-C5 RX characteristic."""
    if _ble_client is None or not _ble_client.is_connected:
        log.warning("BLE not connected – dropping command: %s", cmd)
        return
    payload = (cmd.strip() + "\n").encode()
    mtu_payload = _ble_client.mtu_size - 3
    for i in range(0, len(payload), mtu_payload):
        await _ble_client.write_gatt_char(
            NUS_RX_UUID, payload[i:i + mtu_payload], response=False
        )
    log.info("Sent BLE → %s", cmd.strip())


# ─────────────────────────────────────────────────────────────────────
#  MQTT callbacks  (run in paho's background thread)
# ─────────────────────────────────────────────────────────────────────

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        log.info("MQTT connected to %s:%d", MQTT_BROKER, MQTT_PORT)
        client.subscribe(MQTT_SUB)
        log.info("Subscribed to %s", MQTT_SUB)
    else:
        log.error("MQTT connection failed rc=%d", rc)


def on_message(client, userdata, msg):
    """Received an MQTT message – push a BLE command onto the queue."""
    payload = msg.payload.decode(errors="replace").strip()
    log.info("MQTT ← topic=%s  payload=%s", msg.topic, payload)

    # Accept either a bare channel number ("36") or full command ("ch 36")
    if payload.isdigit():
        cmd = f"ch {payload}"
    elif payload.startswith("ch "):
        cmd = payload
    else:
        log.warning("Unrecognised payload '%s' – ignoring", payload)
        return

    # Thread-safe: schedule the coroutine from the MQTT thread
    if _loop and _cmd_queue:
        _loop.call_soon_threadsafe(_cmd_queue.put_nowait, cmd)


def on_disconnect(client, userdata, rc, properties=None):
    log.warning("MQTT disconnected rc=%d – will auto-reconnect", rc)


# ─────────────────────────────────────────────────────────────────────
#  Main async loop
# ─────────────────────────────────────────────────────────────────────

async def run(mac: str = None):
    global _ble_client, _mqtt_client, _loop, _cmd_queue

    _loop      = asyncio.get_running_loop()
    _cmd_queue = asyncio.Queue()

    # ── MQTT setup ──────────────────────────────────────────────────
    _mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    _mqtt_client.on_connect    = on_connect
    _mqtt_client.on_message    = on_message
    _mqtt_client.on_disconnect = on_disconnect

    log.info("Connecting to MQTT broker %s:%d ...", MQTT_BROKER, MQTT_PORT)
    _mqtt_client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
    _mqtt_client.loop_start()   # runs paho in its own background thread

    # ── BLE setup ───────────────────────────────────────────────────
    address = await find_device(mac)
    log.info("Connecting to BLE %s ...", address)

    async with BleakClient(address, timeout=15.0) as client:
        _ble_client = client
        log.info("BLE connected. MTU=%d", client.mtu_size)

        await client.start_notify(NUS_TX_UUID, ble_rx_handler)
        log.info("Listening on MQTT %s  –  ready", MQTT_SUB)

        # ── Main loop: drain the command queue ───────────────────
        try:
            while True:
                cmd = await _cmd_queue.get()
                await ble_send(cmd)
        except asyncio.CancelledError:
            pass

    _mqtt_client.loop_stop()
    _mqtt_client.disconnect()


# ─────────────────────────────────────────────────────────────────────
#  Entry point
# ─────────────────────────────────────────────────────────────────────

def main():
    mac = sys.argv[1] if len(sys.argv) > 1 else None
    try:
        asyncio.run(run(mac))
    except KeyboardInterrupt:
        log.info("Shutting down.")


if __name__ == "__main__":
    main()
