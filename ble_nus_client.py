#!/usr/bin/env python3
"""
ble_nus_client.py  –  BLE NUS client for iMX6 (Debian)

Connects to the ESP32-C5 BLE NUS server and controls the WiFi AP channel
via MQTT messages on topic /VAL200/channel.

Features:
  - Auto-reconnect if the ESP32-C5 BLE link drops (e.g. power loss)
  - MQTT status published to /VAL200/channel/status  (ONLINE / OFFLINE)
  - Channel commands queued while disconnected, replayed on reconnect
  - Responses published to /VAL200/channel/response

Dependencies (Debian packages):
    sudo apt install python3-bleak python3-paho-mqtt

Usage:
    python3 ble_nus_client.py                    # auto-scan
    python3 ble_nus_client.py XX:XX:XX:XX:XX:XX  # connect by MAC
"""

import asyncio
import sys
import logging
import paho.mqtt.client as mqtt

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

# ── BLE NUS UUIDs ─────────────────────────────────────────────────────
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
DEVICE_NAME = "ESP32C5_NUS"

# ── MQTT config ───────────────────────────────────────────────────────
MQTT_BROKER   = "192.168.99.1"
MQTT_PORT     = 1883
MQTT_SUB      = "/VAL200/channel"
MQTT_PUB      = "/VAL200/channel/response"
MQTT_STATUS   = "/VAL200/channel/status"    # ONLINE / OFFLINE

# ── Reconnect timing ──────────────────────────────────────────────────
RECONNECT_DELAY_MIN = 2    # seconds before first retry
RECONNECT_DELAY_MAX = 30   # cap backoff at this

# ── Logging ───────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-7s  %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("ble_nus")

# ── Shared state ──────────────────────────────────────────────────────
_cmd_queue:   asyncio.Queue              = None
_mqtt_client: mqtt.Client               = None
_loop:        asyncio.AbstractEventLoop = None
_ble_connected: bool                    = False


# ─────────────────────────────────────────────────────────────────────
#  MQTT helpers
# ─────────────────────────────────────────────────────────────────────

def mqtt_publish_status(status: str):
    """Publish ONLINE or OFFLINE to the status topic."""
    if _mqtt_client and _mqtt_client.is_connected():
        _mqtt_client.publish(MQTT_STATUS, status, retain=True)
        log.info("MQTT status → %s", status)


def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        log.info("MQTT connected to %s:%d", MQTT_BROKER, MQTT_PORT)
        client.subscribe(MQTT_SUB)
        log.info("Subscribed to %s", MQTT_SUB)
        # Re-publish current BLE state on MQTT reconnect
        mqtt_publish_status("ONLINE" if _ble_connected else "OFFLINE")
    else:
        log.error("MQTT connection failed rc=%d", rc)


def on_message(client, userdata, msg):
    payload = msg.payload.decode(errors="replace").strip()
    log.info("MQTT ← topic=%s  payload=%s", msg.topic, payload)

    if payload.isdigit():
        cmd = f"ch {payload}"
    elif payload.startswith("ch "):
        cmd = payload
    else:
        log.warning("Unrecognised payload '%s' – ignoring", payload)
        return

    if _loop and _cmd_queue:
        _loop.call_soon_threadsafe(_cmd_queue.put_nowait, cmd)


def on_disconnect(client, userdata, rc, properties=None):
    log.warning("MQTT disconnected rc=%d – will auto-reconnect", rc)


# ─────────────────────────────────────────────────────────────────────
#  BLE helpers
# ─────────────────────────────────────────────────────────────────────

def ble_rx_handler(sender, data: bytearray):
    text = data.decode(errors="replace").strip()
    if text:
        log.info("ESP32-C5 → %s", text)
        if _mqtt_client and _mqtt_client.is_connected():
            _mqtt_client.publish(MQTT_PUB, text)


def ble_disconnect_callback(client: BleakClient):
    """Fired by bleak when the BLE link drops unexpectedly."""
    global _ble_connected
    _ble_connected = False
    log.warning("BLE link lost – ESP32-C5 disconnected")
    mqtt_publish_status("OFFLINE")
    # Wake the reconnect loop by signalling via the queue
    if _loop and _cmd_queue:
        _loop.call_soon_threadsafe(_cmd_queue.put_nowait, "__RECONNECT__")


async def find_device(mac: str = None) -> str:
    if mac:
        return mac
    log.info("Scanning for '%s'...", DEVICE_NAME)
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
    if device is None:
        return None
    log.info("Found: %s  [%s]", device.name, device.address)
    return device.address


async def ble_send(client: BleakClient, cmd: str):
    payload = (cmd.strip() + "\n").encode()
    mtu_payload = max(client.mtu_size - 3, 20)
    for i in range(0, len(payload), mtu_payload):
        await client.write_gatt_char(
            NUS_RX_UUID, payload[i:i + mtu_payload], response=False
        )
    log.info("Sent BLE → %s", cmd.strip())


# ─────────────────────────────────────────────────────────────────────
#  BLE session – single connect attempt, runs until link drops
# ─────────────────────────────────────────────────────────────────────

async def ble_session(address: str):
    """
    Connect, handle commands until disconnect, then return.
    Caller handles reconnect loop and backoff.
    """
    global _ble_connected

    try:
        async with BleakClient(
            address,
            timeout=15.0,
            disconnected_callback=ble_disconnect_callback
        ) as client:

            _ble_connected = True
            log.info("BLE connected  [%s]  MTU=%d", address, client.mtu_size)
            mqtt_publish_status("ONLINE")
            await client.start_notify(NUS_TX_UUID, ble_rx_handler)
            log.info("Listening on MQTT %s  –  ready", MQTT_SUB)

            while True:
                cmd = await _cmd_queue.get()

                if cmd == "__RECONNECT__":
                    # Disconnect callback already fired – exit session
                    log.info("Reconnect signal received, ending session")
                    break

                if not client.is_connected:
                    # Put command back so it can be replayed after reconnect
                    await _cmd_queue.put(cmd)
                    break

                try:
                    await ble_send(client, cmd)
                except BleakError as e:
                    log.error("BLE send failed: %s – requeueing command", e)
                    await _cmd_queue.put(cmd)
                    break

    except (BleakError, asyncio.TimeoutError, OSError) as e:
        log.warning("BLE session error: %s", e)
        _ble_connected = False
        mqtt_publish_status("OFFLINE")


# ─────────────────────────────────────────────────────────────────────
#  Reconnect loop with exponential backoff
# ─────────────────────────────────────────────────────────────────────

async def ble_reconnect_loop(mac: str = None):
    """Outer loop: scan → connect → session → repeat on failure."""
    delay = RECONNECT_DELAY_MIN
    address = mac  # None = scan each time

    while True:
        # Resolve address if not fixed
        if address is None:
            resolved = await find_device()
            if resolved is None:
                log.warning("Device not found, retrying in %ds...", delay)
                await asyncio.sleep(delay)
                delay = min(delay * 2, RECONNECT_DELAY_MAX)
                continue
            address = resolved

        await ble_session(address)

        if not _ble_connected:
            log.info("Reconnecting in %ds...", delay)
            await asyncio.sleep(delay)
            delay = min(delay * 2, RECONNECT_DELAY_MAX)
            # If no fixed MAC, re-scan on next attempt
            if mac is None:
                address = None
        else:
            # Clean exit (shouldn't happen in normal operation)
            break


# ─────────────────────────────────────────────────────────────────────
#  Main
# ─────────────────────────────────────────────────────────────────────

async def run(mac: str = None):
    global _mqtt_client, _loop, _cmd_queue

    _loop      = asyncio.get_running_loop()
    _cmd_queue = asyncio.Queue()

    # ── MQTT ────────────────────────────────────────────────────────
    _mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    _mqtt_client.on_connect    = on_connect
    _mqtt_client.on_message    = on_message
    _mqtt_client.on_disconnect = on_disconnect
    # Last-will so broker publishes OFFLINE if the iMX6 process dies
    _mqtt_client.will_set(MQTT_STATUS, "OFFLINE", retain=True)

    log.info("Connecting to MQTT broker %s:%d ...", MQTT_BROKER, MQTT_PORT)
    _mqtt_client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
    _mqtt_client.loop_start()

    # ── BLE reconnect loop ──────────────────────────────────────────
    try:
        await ble_reconnect_loop(mac)
    finally:
        mqtt_publish_status("OFFLINE")
        _mqtt_client.loop_stop()
        _mqtt_client.disconnect()


def main():
    mac = sys.argv[1] if len(sys.argv) > 1 else None
    try:
        asyncio.run(run(mac))
    except KeyboardInterrupt:
        log.info("Shutting down.")


if __name__ == "__main__":
    main()
