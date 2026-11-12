#!/usr/bin/env python3
"""
Stream RGB frames to ESP32 over Wi-Fi using DDP (Distributed Display Protocol, UDP port 4048).
Includes AlexGyver ColorMusic VU-meter simulation mode!
"""
import argparse
import colorsys
import math
import socket
import struct
import time

ESP32_IP = "192.168.2.168"
DDP_PORT = 4048
NUM_LEDS = 100


def build_ddp_packet(rgb_bytes: bytes, offset: int = 0, push: bool = True) -> bytes:
    """
    Build a 10-byte standard DDP header + RGB payload.
    """
    flags = 0x41 if push else 0x40
    header = struct.pack(">BBBBIH", flags, 0, 0x01, 0x01, offset, len(rgb_bytes))
    return header + rgb_bytes


def stream(ip: str, port: int, num_leds: int, mode: str = "gyver", duration: float = 0.0, fps: int = 40):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    start_time = time.time()
    frame = 0
    print(f"Streaming '{mode}' to {ip}:{port} ({num_leds} LEDs at {fps} FPS)... Press Ctrl+C to stop.")
    try:
        while True:
            t = time.time() - start_time
            if duration > 0 and t >= duration:
                break
            buf = bytearray(num_leds * 3)

            if mode == "gyver":
                # Simulates AlexGyver Mode 0 (Center-out VU meter with dim purple background)
                half = num_leds // 2
                # Simulated music beat (0.0 to 1.0)
                beat = abs(math.sin(t * 3.5)) * (0.5 + 0.5 * abs(math.sin(t * 1.1)))
                active_len = int(beat * half)

                for i in range(num_leds):
                    dist = abs(i - half)
                    idx = i * 3
                    if dist < active_len:
                        # Green -> Yellow -> Red gradient from center outward (like soundlevel_gp)
                        ratio = dist / max(1, half)
                        if ratio < 0.5:
                            r = int(255 * (ratio * 2))
                            g = 255
                            b = 0
                        else:
                            r = 255
                            g = int(255 * (1.0 - (ratio - 0.5) * 2))
                            b = 0
                        buf[idx] = r
                        buf[idx + 1] = g
                        buf[idx + 2] = b
                    else:
                        # EMPTY_COLOR = HUE_PURPLE, EMPTY_BRIGHT = 30
                        buf[idx] = 20
                        buf[idx + 1] = 0
                        buf[idx + 2] = 30

            elif mode == "chase":
                pos = frame % num_leds
                idx = pos * 3
                buf[idx] = 255
                buf[idx + 1] = 255
                buf[idx + 2] = 255

            elif mode == "markers":
                for i in range(0, num_leds, 10):
                    idx = i * 3
                    buf[idx] = 255 if (i // 10) % 2 == 0 else 0
                    buf[idx + 1] = 255 if (i // 10) % 2 == 1 else 0
                    buf[idx + 2] = 0

            else:
                for i in range(num_leds):
                    hue = ((i / num_leds) + (frame * 0.015)) % 1.0
                    r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
                    idx = i * 3
                    buf[idx] = int(r * 255)
                    buf[idx + 1] = int(g * 255)
                    buf[idx + 2] = int(b * 255)

            packet = build_ddp_packet(bytes(buf), offset=0, push=True)
            sock.sendto(packet, (ip, port))
            frame += 1
            time.sleep(1.0 / fps)
    except KeyboardInterrupt:
        print("\nStopped streaming.")
    finally:
        sock.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stream DDP LED data to ESP32 over Wi-Fi")
    parser.add_argument("--ip", default=ESP32_IP, help="ESP32 IP address")
    parser.add_argument("--port", type=int, default=DDP_PORT, help="DDP UDP port (default: 4048)")
    parser.add_argument("--leds", type=int, default=NUM_LEDS, help="Number of LEDs (default: 100)")
    parser.add_argument("--mode", choices=["gyver", "rainbow", "chase", "markers"], default="gyver", help="Test mode")
    parser.add_argument("--duration", type=float, default=0.0, help="Duration in seconds (0 = infinite)")
    args = parser.parse_args()

    stream(args.ip, args.port, args.leds, args.mode, args.duration)
