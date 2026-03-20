#!/usr/bin/env python3
"""
ble_nus_client.py  –  BLE NUS client for iMX6 (Debian)

Connects to the ESP32-C5 BLE NUS server and provides an interactive
terminal for sending channel-control commands.

Install dependency:
    pip install bleak

Usage:
    python3 ble_nus_client.py                  # auto-scan for ESP32C5_NUS
    python3 ble_nus_client.py XX:XX:XX:XX:XX:XX  # connect by MAC address
"""

import asyncio
import sys
from bleak import BleakClient, BleakScanner

# Standard Nordic UART Service UUIDs
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write to ESP32
NUS_TX_UUID      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify from ESP32

DEVICE_NAME = "ESP32C5_NUS"


def rx_handler(sender, data: bytearray):
    """Called when ESP32-C5 sends a notification (response text)."""
    print(data.decode(errors="replace"), end="", flush=True)


async def find_device(mac: str = None):
    """Scan for the ESP32-C5 NUS device, or use provided MAC."""
    if mac:
        return mac

    print(f"Scanning for '{DEVICE_NAME}'...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
    if device is None:
        print(f"ERROR: '{DEVICE_NAME}' not found. Is the ESP32-C5 powered on?")
        sys.exit(1)
    print(f"Found: {device.name}  [{device.address}]")
    return device.address


async def run(mac: str = None):
    address = await find_device(mac)

    print(f"Connecting to {address} ...")
    async with BleakClient(address, timeout=15.0) as client:
        print(f"Connected. MTU={client.mtu_size}")

        # Subscribe to TX notifications (responses from ESP32-C5)
        await client.start_notify(NUS_TX_UUID, rx_handler)

        print("Type commands and press Enter.  Ctrl-C to quit.\n")

        loop = asyncio.get_event_loop()

        while True:
            # Read input without blocking the event loop
            line = await loop.run_in_executor(None, input, "")
            if not line:
                continue
            cmd = (line.strip() + "\n").encode()
            # BLE write – split into MTU-sized chunks if needed
            mtu = client.mtu_size - 3  # subtract ATT header
            for i in range(0, len(cmd), mtu):
                await client.write_gatt_char(NUS_RX_UUID,
                                             cmd[i:i+mtu],
                                             response=False)


def main():
    mac = sys.argv[1] if len(sys.argv) > 1 else None
    try:
        asyncio.run(run(mac))
    except KeyboardInterrupt:
        print("\nDisconnected.")


if __name__ == "__main__":
    main()
