#!/usr/bin/env python3
"""Home Assistant AppDaemon App: Camera to ESP32 Image Display (single upload)

Fetches a camera snapshot from Home Assistant and sends it to an ESP32 device
running this firmware's Image API.

Differences vs the sample implementation:
- Only supports *single* upload mode:
    POST /api/display/image?timeout=<seconds>
- Automatically detects the target panel resolution via:
    GET  /api/info  (display_coord_width / display_coord_height)

Workflow:
  1) Fetch camera snapshot from Home Assistant (camera_proxy)
  2) Optional rotation (auto/no/explicit)
  3) Resize with letterboxing to ESP32 panel coordinate space
  4) Encode as baseline JPEG (TJpgDec-friendly)
  5) Upload via multipart/form-data

Installation (Home Assistant OS / AppDaemon add-on):
  1. Copy this file to:
     /addon_configs/a0d7b954_appdaemon/apps/camera_to_esp32.py
  2. Add to apps.yaml:

     camera_to_esp32:
       module: camera_to_esp32
       class: CameraToESP32
       # optional defaults:
       # default_esp32_ip: "192.168.1.111"
       # jpeg_quality: 80
       # rotate_degrees: null|0|90|180|270

Usage (automation action):
  - event: camera_to_esp32
    event_data:
      camera_entity: camera.front_door
      esp32_ip: "192.168.1.111"   # optional if default_esp32_ip set
      timeout: 10                 # optional (0 = permanent)
      rotate_degrees: null        # optional: null=auto, 0=no rotate, 90/180/270
      jpeg_quality: 80            # optional
      dismiss: false              # optional: if true, only dismisses current image

Requirements:
  - Pillow (add to AppDaemon python_packages)
"""

from __future__ import annotations

import io
import os
from typing import Any, Dict, Optional, Tuple

import appdaemon.plugins.hass.hassapi as hass
import requests
from PIL import Image


def _lanczos_resample() -> int:
    # Pillow >= 9 uses Image.Resampling; older versions expose Image.LANCZOS.
    try:
        return Image.Resampling.LANCZOS  # type: ignore[attr-defined]
    except Exception:
        return Image.LANCZOS  # type: ignore[attr-defined]


class CameraToESP32(hass.Hass):
    def initialize(self):
        self.log("CameraToESP32 initialized")

        self.default_esp32_ip = self.args.get("default_esp32_ip", None)

        # Default JPEG quality for re-encoding (baseline JPEG, TJpgDec-friendly)
        self.default_jpeg_quality = int(self.args.get("jpeg_quality", 80))

        # Rotation control:
        #   - None: auto-rotate landscape->portrait when targeting a portrait panel
        #   - 0: no rotation
        #   - 90/180/270: explicit rotation
        raw_rotate_degrees = self.args.get("rotate_degrees", None)
        self.default_rotate_degrees: Optional[int] = None if raw_rotate_degrees is None else int(raw_rotate_degrees)

        # Legacy boolean rotate flag support (kept for compatibility with older configs)
        raw_rotate = self.args.get("rotate", None)
        if raw_rotate is not None and raw_rotate_degrees is None:
            self.default_rotate_degrees = None if bool(raw_rotate) else 0

        # Event listener
        self.listen_event(self.handle_event, "camera_to_esp32")

    def handle_event(self, event_name: str, data: Dict[str, Any], kwargs: Dict[str, Any]):
        self.log(f"Event data received: {data}")

        camera_entity = data.get("camera_entity")
        esp32_ip = data.get("esp32_ip", self.default_esp32_ip)

        dismiss = bool(data.get("dismiss", False))
        timeout = int(data.get("timeout", 10))
        jpeg_quality = int(data.get("jpeg_quality", self.default_jpeg_quality))

        rotate_degrees = data.get("rotate_degrees", self.default_rotate_degrees)
        if "rotate_degrees" not in data and "rotate" in data:
            rotate_degrees = None if bool(data.get("rotate")) else 0

        if not esp32_ip:
            self.error("Missing required parameter: esp32_ip (or set default_esp32_ip in apps.yaml)")
            return

        if dismiss:
            self.log(f"Dismissing image on {esp32_ip}")
            ok = self.dismiss_image(esp32_ip)
            if ok:
                self.log("[OK] Dismiss successful")
            else:
                self.error("Dismiss failed")
            return

        if not camera_entity:
            self.error("Missing required parameter: camera_entity")
            return

        rotate_desc = "auto" if rotate_degrees is None else str(rotate_degrees)

        if rotate_degrees is not None:
            try:
                rotate_degrees = int(rotate_degrees)
            except Exception:
                self.error(f"Invalid rotate_degrees '{rotate_degrees}'. Expected null/0/90/180/270")
                return
            if rotate_degrees not in (0, 90, 180, 270):
                self.error(f"Invalid rotate_degrees '{rotate_degrees}'. Expected 0/90/180/270")
                return

        if jpeg_quality < 1 or jpeg_quality > 100:
            self.error(f"Invalid jpeg_quality '{jpeg_quality}'. Expected 1-100")
            return

        self.log(
            f"Processing snapshot: {camera_entity} -> {esp32_ip} "
            f"(timeout={timeout}s, rotate_degrees={rotate_desc}, jpeg_quality={jpeg_quality})"
        )

        self.process_camera_snapshot(
            camera_entity=camera_entity,
            esp32_ip=esp32_ip,
            timeout=timeout,
            rotate_degrees=rotate_degrees,
            jpeg_quality=jpeg_quality,
        )

    def process_camera_snapshot(
        self,
        camera_entity: str,
        esp32_ip: str,
        timeout: int = 10,
        rotate_degrees: Optional[int] = None,
        jpeg_quality: int = 80,
    ):
        try:
            self.log(f"Fetching snapshot from {camera_entity}")
            snapshot = self.get_camera_snapshot(camera_entity)
            if not snapshot:
                self.error(f"Failed to fetch camera snapshot from {camera_entity}")
                return

            self.log(f"Fetched camera snapshot: {len(snapshot)} bytes")

            self.log(f"Fetching ESP32 info from {esp32_ip}")
            display_w, display_h = self.get_esp32_display_resolution(esp32_ip)
            self.log(f"ESP32 display resolution: {display_w}x{display_h}")

            self.log("Preparing image (rotate + letterbox)")
            img = self.prepare_image(
                snapshot,
                target_width=display_w,
                target_height=display_h,
                rotate_degrees=rotate_degrees,
            )
            if img is None:
                self.error("Failed to prepare image")
                return

            out_jpeg = self.encode_baseline_jpeg(img, quality=jpeg_quality)
            self.log(f"Uploading to {esp32_ip} (timeout={timeout}s, bytes={len(out_jpeg)})")

            ok = self.upload_single(out_jpeg, esp32_ip, timeout)
            if ok:
                self.log("[OK] Upload successful")
            else:
                self.error("Upload failed")

        except Exception as e:
            self.error(f"Error processing camera snapshot: {e}", level="ERROR")

    def get_camera_snapshot(self, camera_entity: str) -> Optional[bytes]:
        try:
            url = f"{self.get_ha_url()}/api/camera_proxy/{camera_entity}"
            headers = {
                "Authorization": f"Bearer {self.get_ha_token()}",
            }
            response = requests.get(url, headers=headers, timeout=15)
            if response.status_code == 200:
                return response.content
            self.error(f"Camera snapshot failed: HTTP {response.status_code}")
            return None
        except Exception as e:
            self.error(f"Failed to fetch camera snapshot: {e}")
            return None

    def get_esp32_display_resolution(self, esp32_ip: str) -> Tuple[int, int]:
        """Read /api/info and return (display_coord_width, display_coord_height)."""
        url = f"http://{esp32_ip}/api/info"
        try:
            response = requests.get(url, timeout=5)
            if response.status_code != 200:
                raise RuntimeError(f"HTTP {response.status_code}: {response.text}")

            info = response.json()

            if not bool(info.get("has_display", False)):
                raise RuntimeError("Device reports has_display=false")

            w = info.get("display_coord_width")
            h = info.get("display_coord_height")
            if not isinstance(w, int) or not isinstance(h, int) or w <= 0 or h <= 0:
                raise RuntimeError("Missing/invalid display_coord_width/height in /api/info")

            return w, h

        except Exception as e:
            # Conservative fallback: keep a sane default if info API is unavailable.
            # This avoids hard failure during transient network issues.
            self.error(f"Failed to read display resolution from /api/info: {e} (fallback 240x280)")
            return 240, 280

    def prepare_image(
        self,
        jpeg_data: bytes,
        target_width: int,
        target_height: int,
        rotate_degrees: Optional[int] = None,
    ) -> Optional[Image.Image]:
        """Decode JPEG bytes into RGB PIL image, rotate (auto/no/explicit), then letterbox resize."""
        try:
            img = Image.open(io.BytesIO(jpeg_data))
            original_w, original_h = img.size

            # Rotation
            if rotate_degrees is None:
                # Auto-rotate landscape -> portrait when targeting a portrait panel.
                if original_w > original_h and target_height > target_width:
                    self.log(f"Auto-rotating landscape snapshot ({original_w}x{original_h})")
                    img = img.transpose(Image.ROTATE_90)
                    original_w, original_h = img.size
            elif rotate_degrees != 0:
                # PIL rotates counter-clockwise for positive degrees.
                self.log(f"Rotating by {rotate_degrees} degrees")
                img = img.rotate(rotate_degrees, expand=True)
                original_w, original_h = img.size

            # Convert to RGB (handles RGBA, grayscale, etc.)
            if img.mode != "RGB":
                img = img.convert("RGB")

            # Letterbox resize
            width_ratio = target_width / float(original_w)
            height_ratio = target_height / float(original_h)
            scale_ratio = min(width_ratio, height_ratio)

            new_w = max(1, int(original_w * scale_ratio))
            new_h = max(1, int(original_h * scale_ratio))

            resample = _lanczos_resample()
            img_resized = img.resize((new_w, new_h), resample)

            background = Image.new("RGB", (target_width, target_height), (0, 0, 0))
            offset_x = (target_width - new_w) // 2
            offset_y = (target_height - new_h) // 2
            background.paste(img_resized, (offset_x, offset_y))

            return background

        except Exception as e:
            self.error(f"Failed to prepare image: {e}")
            return None

    def encode_baseline_jpeg(self, img: Image.Image, quality: int = 80) -> bytes:
        output = io.BytesIO()
        img.save(
            output,
            format="JPEG",
            quality=quality,
            subsampling=2,  # 4:2:0
            progressive=False,
            optimize=False,
        )
        return output.getvalue()

    def upload_single(self, jpeg_data: bytes, esp32_ip: str, timeout: int = 10) -> bool:
        """POST multipart JPEG to /api/display/image?timeout=..."""
        try:
            url = f"http://{esp32_ip}/api/display/image"
            params = {}
            if timeout is not None:
                params["timeout"] = int(timeout)

            files = {"file": ("image.jpg", jpeg_data, "image/jpeg")}
            response = requests.post(url, params=params, files=files, timeout=20)

            self.log(f"Upload response: HTTP {response.status_code} {response.text}")
            if response.status_code != 200:
                return False

            # Firmware responds with JSON {success: true, ...}
            try:
                payload = response.json()
                return bool(payload.get("success", False))
            except Exception:
                # Fallback: accept HTTP 200 if JSON parsing fails
                return True

        except Exception as e:
            self.error(f"Upload error: {e}")
            return False

    def dismiss_image(self, esp32_ip: str) -> bool:
        """DELETE /api/display/image"""
        try:
            url = f"http://{esp32_ip}/api/display/image"
            response = requests.delete(url, timeout=10)
            self.log(f"Dismiss response: HTTP {response.status_code} {response.text}")
            if response.status_code != 200:
                return False

            try:
                payload = response.json()
                return bool(payload.get("success", False))
            except Exception:
                return True

        except Exception as e:
            self.error(f"Dismiss error: {e}")
            return False

    def get_ha_url(self) -> str:
        # In Home Assistant OS/Supervised, Supervisor provides a stable internal URL.
        return "http://supervisor/core"

    def get_ha_token(self) -> str:
        # Supervisor token is available to add-ons.
        return os.environ.get("SUPERVISOR_TOKEN", "")
