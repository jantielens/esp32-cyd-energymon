#!/usr/bin/env python3
"""
Upload and Display Images on ESP32 Devices

Uploads JPEG images to ESP32 devices running the firmware with HAS_IMAGE_API enabled.
Supports full image upload or memory-efficient strip mode.
Can generate test images with gradient patterns.

Usage:
    # Upload local image
    ./upload_image.py 192.168.1.100 --image photo.jpg
    
    # Upload in strip mode (memory efficient)
    ./upload_image.py 192.168.1.100 --image photo.jpg --mode strip
    
    # Generate and upload test image
    ./upload_image.py 192.168.1.100 --generate 320x240
    
    # Use mDNS hostname
    ./upload_image.py esp32-device.local --image photo.jpg --timeout 30

Dependencies:
    pip install requests pillow
"""

import argparse
import sys
import os
import io
import random
import requests
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
from typing import Optional, Tuple

# ANSI colors for terminal output
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'


def print_header(msg: str):
    print(f"{Colors.HEADER}{Colors.BOLD}=== {msg} ==={Colors.ENDC}")


def print_success(msg: str):
    print(f"{Colors.OKGREEN}✓ {msg}{Colors.ENDC}")


def print_error(msg: str):
    print(f"{Colors.FAIL}✗ {msg}{Colors.ENDC}", file=sys.stderr)


def print_info(msg: str):
    print(f"{Colors.OKCYAN}ℹ {msg}{Colors.ENDC}")


def generate_test_image(width: int, height: int, quality: int = 85, seed: Optional[int] = None) -> bytes:
    """
    Generate a test image with color gradient bars (similar to test screen).
    Returns JPEG bytes.
    """
    seed_msg = f", seed {seed}" if seed is not None else ""
    print_info(f"Generating test image: {width}x{height} (quality {quality}%){seed_msg}")
    
    # Create image
    img = Image.new('RGB', (width, height))
    draw = ImageDraw.Draw(img)
    
    # Number of gradient bars
    num_bars = 7
    bar_height = height // num_bars
    
    # Define gradient colors (similar to test screen)
    colors = [
        (255, 0, 0),      # Red
        (255, 127, 0),    # Orange
        (255, 255, 0),    # Yellow
        (0, 255, 0),      # Green
        (0, 127, 255),    # Cyan
        (0, 0, 255),      # Blue
        (139, 0, 255),    # Violet
    ]
    
    # Draw gradient bars
    for i in range(num_bars):
        y_start = i * bar_height
        y_end = y_start + bar_height if i < num_bars - 1 else height
        
        # Get start and end colors
        start_color = colors[i]
        end_color = colors[(i + 1) % len(colors)]
        
        # Draw gradient for this bar
        for y in range(y_start, y_end):
            # Calculate gradient position (0.0 to 1.0)
            t = (y - y_start) / (y_end - y_start)
            
            # Interpolate color
            r = int(start_color[0] * (1 - t) + end_color[0] * t)
            g = int(start_color[1] * (1 - t) + end_color[1] * t)
            b = int(start_color[2] * (1 - t) + end_color[2] * t)
            
            draw.line([(0, y), (width, y)], fill=(r, g, b))

    # Optional deterministic variation to avoid repeatedly uploading the exact same JPEG.
    # This intentionally changes compressibility and stresses a slightly wider range of
    # JPEG sizes/entropy while staying fast and dependency-free.
    if seed is not None:
        rng = random.Random(int(seed))
        # Add a set of random solid rectangles.
        rect_count = 40
        max_w = max(8, width // 5)
        max_h = max(8, height // 5)
        for _ in range(rect_count):
            x0 = rng.randrange(0, width)
            y0 = rng.randrange(0, height)
            rw = rng.randrange(4, max_w)
            rh = rng.randrange(4, max_h)
            x1 = min(width, x0 + rw)
            y1 = min(height, y0 + rh)
            color = (rng.randrange(256), rng.randrange(256), rng.randrange(256))
            draw.rectangle([x0, y0, x1, y1], fill=color)
    
    # Add resolution text overlay in center
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 24)
    except Exception:
        font = ImageFont.load_default()
    
    text = f"{width}x{height}"
    if seed is not None:
        text = f"{text}  seed={seed}"
    # Get text bounding box for centering
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    
    text_x = (width - text_width) // 2
    text_y = (height - text_height) // 2
    
    # Draw text with outline for better visibility
    outline_width = 2
    for dx in range(-outline_width, outline_width + 1):
        for dy in range(-outline_width, outline_width + 1):
            if dx != 0 or dy != 0:
                draw.text((text_x + dx, text_y + dy), text, font=font, fill=(0, 0, 0))
    draw.text((text_x, text_y), text, font=font, fill=(255, 255, 255))
    
    # Convert to JPEG bytes
    buffer = io.BytesIO()
    img.save(buffer, format='JPEG', quality=quality, optimize=True)
    jpeg_bytes = buffer.getvalue()
    
    print_success(f"Generated {len(jpeg_bytes)} byte JPEG (quality {quality}%)")
    return jpeg_bytes


def generate_worst_case_image(width: int, height: int, quality: int = 85, seed: Optional[int] = None) -> bytes:
    """Generate a deliberately less-compressible (worst-case) RGB noise image.

    This stresses the firmware's upload buffering and allocation behavior more realistically
    than simple gradients, because entropy is high and JPEGs tend to be larger.

    Deterministic when seed is provided.
    """

    seed_msg = f", seed {seed}" if seed is not None else ""
    print_info(f"Generating WORST-CASE noise image: {width}x{height} (quality {quality}%){seed_msg}")

    if seed is not None:
        rng = random.Random(int(seed))
    else:
        rng = random.SystemRandom()

    # Deterministic, fast-ish random bytes. Python's Random has randbytes().
    # 3 bytes per pixel for RGB.
    n = int(width) * int(height) * 3
    try:
        raw = rng.randbytes(n)  # type: ignore[attr-defined]
    except Exception:
        # Fallback for older Python
        raw = bytes(rng.getrandbits(8) for _ in range(n))

    img = Image.frombytes('RGB', (width, height), raw)
    draw = ImageDraw.Draw(img)

    # Small overlay so we can see which run/seed created the image.
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
    except Exception:
        font = ImageFont.load_default()

    text = f"{width}x{height}"
    if seed is not None:
        text = f"{text} seed={seed}"

    # Semi-transparent box (approx) by drawing a filled rectangle behind text.
    bbox = draw.textbbox((0, 0), text, font=font)
    pad = 6
    x0, y0 = 6, 6
    x1 = x0 + (bbox[2] - bbox[0]) + pad * 2
    y1 = y0 + (bbox[3] - bbox[1]) + pad * 2
    draw.rectangle([x0, y0, x1, y1], fill=(0, 0, 0))
    draw.text((x0 + pad, y0 + pad), text, font=font, fill=(255, 255, 255))

    buffer = io.BytesIO()
    img.save(buffer, format='JPEG', quality=quality, optimize=True)
    jpeg_bytes = buffer.getvalue()
    print_success(f"Generated {len(jpeg_bytes)} byte JPEG (quality {quality}%)")
    return jpeg_bytes


def upload_full_image(host: str, jpeg_data: bytes, timeout: int, verbose: bool = False) -> bool:
    """Upload full JPEG image (deferred decode mode)."""
    url = f"http://{host}/api/display/image"
    if timeout > 0:
        url += f"?timeout={timeout}"
    
    print_info(f"Uploading {len(jpeg_data)} bytes to {url}")
    
    try:
        # Send as multipart/form-data file upload
        files = {'file': ('image.jpg', jpeg_data, 'image/jpeg')}
        response = requests.post(url, files=files, timeout=30)
        
        if verbose:
            print(f"Response: {response.status_code} {response.text}")
        
        if response.status_code == 200:
            result = response.json()
            print_success(result.get('message', 'Image uploaded successfully'))
            return True
        else:
            try:
                error = response.json()
                print_error(f"Upload failed: {error.get('message', response.text)}")
            except Exception:
                print_error(f"Upload failed: {response.status_code} {response.text}")
            return False
            
    except requests.exceptions.RequestException as e:
        print_error(f"Network error: {e}")
        return False


def split_jpeg_into_strips(jpeg_data: bytes, strip_height: int, quality: int = 85) -> list:
    """
    Split JPEG image into horizontal strips.
    Returns list of (strip_index, strip_jpeg_bytes).
    """
    # Load original image
    img = Image.open(io.BytesIO(jpeg_data))
    width, height = img.size
    
    # Calculate number of strips
    num_strips = (height + strip_height - 1) // strip_height  # Ceiling division
    
    strips = []
    for i in range(num_strips):
        y_start = i * strip_height
        y_end = min(y_start + strip_height, height)
        
        # Crop strip
        strip_img = img.crop((0, y_start, width, y_end))
        
        # Convert to JPEG
        buffer = io.BytesIO()
        strip_img.save(buffer, format='JPEG', quality=quality, optimize=True)
        strip_bytes = buffer.getvalue()
        
        strips.append((i, strip_bytes))
    
    return strips, width, height


def upload_strip_mode(host: str, jpeg_data: bytes, strip_height: int, timeout: int, quality: int = 85, verbose: bool = False) -> bool:
    """Upload image in strip mode (synchronous decode, memory efficient)."""
    print_info(f"Splitting image into {strip_height}px strips (quality {quality}%)...")
    
    strips, width, height = split_jpeg_into_strips(jpeg_data, strip_height, quality)
    num_strips = len(strips)
    
    print_info(f"Image: {width}x{height}, {num_strips} strips")
    
    url = f"http://{host}/api/display/image/strips"
    
    for strip_index, strip_data in strips:
        params = {
            'strip_index': strip_index,
            'strip_count': num_strips,
            'width': width,
            'height': height,
        }
        
        # Only add timeout on first strip
        if strip_index == 0 and timeout > 0:
            params['timeout'] = timeout
        
        print(f"  Uploading strip {strip_index + 1}/{num_strips} ({len(strip_data)} bytes)...", end='', flush=True)
        
        try:
            # Send as raw POST body data
            headers = {'Content-Type': 'image/jpeg'}
            response = requests.post(url, params=params, data=strip_data, headers=headers, timeout=30)
            
            if verbose:
                print(f"\n  Response: {response.status_code} {response.text}")
            
            if response.status_code == 200:
                print(f" {Colors.OKGREEN}✓{Colors.ENDC}")
            else:
                print(f" {Colors.FAIL}✗{Colors.ENDC}")
                try:
                    error = response.json()
                    print_error(f"Strip upload failed: {error.get('message', response.text)}")
                except:
                    print_error(f"Strip upload failed: {response.status_code} {response.text}")
                return False
                
        except requests.exceptions.RequestException as e:
            print(f" {Colors.FAIL}✗{Colors.ENDC}")
            print_error(f"Network error: {e}")
            return False
    
    print_success(f"All {num_strips} strips uploaded successfully")
    return True


def dismiss_image(host: str, verbose: bool = False) -> bool:
    """Dismiss currently displayed image."""
    url = f"http://{host}/api/display/image"
    
    print_info(f"Dismissing image on {host}")
    
    try:
        response = requests.delete(url, timeout=10)
        
        if verbose:
            print(f"Response: {response.status_code} {response.text}")
        
        if response.status_code == 200:
            result = response.json()
            print_success(result.get('message', 'Image dismissed'))
            return True
        else:
            print_error(f"Dismiss failed: {response.status_code}")
            return False
            
    except requests.exceptions.RequestException as e:
        print_error(f"Network error: {e}")
        return False


def parse_resolution(res_str: str) -> Tuple[int, int]:
    """Parse resolution string like '320x240' into (width, height)."""
    parts = res_str.lower().split('x')
    if len(parts) != 2:
        raise ValueError("Resolution must be in format WIDTHxHEIGHT (e.g., 320x240)")
    
    try:
        width = int(parts[0])
        height = int(parts[1])
    except ValueError:
        raise ValueError("Width and height must be integers")
    
    if width <= 0 or height <= 0:
        raise ValueError("Width and height must be positive")
    
    if width > 4096 or height > 4096:
        raise ValueError("Resolution too large (max 4096x4096)")
    
    return width, height


def fetch_info_api(host: str, verbose: bool = False) -> dict:
    """Fetch /api/info JSON from the device."""
    url = f"http://{host}/api/info"
    if verbose:
        print_info(f"Fetching device info: {url}")
    response = requests.get(url, timeout=10)
    response.raise_for_status()
    return response.json()


def get_device_display_coord_resolution(host: str, verbose: bool = False) -> Tuple[int, int]:
    """Resolve display coordinate-space resolution from /api/info."""
    info = fetch_info_api(host, verbose=verbose)
    width = info.get('display_coord_width')
    height = info.get('display_coord_height')

    if not isinstance(width, int) or not isinstance(height, int):
        raise ValueError("Device /api/info did not return integer display_coord_width/display_coord_height")
    if width <= 0 or height <= 0:
        raise ValueError(f"Invalid device resolution from /api/info: {width}x{height}")
    return width, height


def main():
    parser = argparse.ArgumentParser(
        description='Upload and display JPEG images on ESP32 devices',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s 192.168.1.100 --image photo.jpg
  %(prog)s 192.168.1.100 --image photo.jpg --mode strip --strip-height 16
  %(prog)s 192.168.1.100 --generate 320x240 --timeout 30
    %(prog)s 192.168.1.100 --generate --timeout 30   # auto-detect from /api/info
  %(prog)s esp32-cyd.local --generate 240x240
  %(prog)s 192.168.1.100 --dismiss
        """
    )
    
    parser.add_argument('host', nargs='?', help='Device IP address or mDNS hostname')
    
    # Image source (mutually exclusive)
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument('--image', metavar='PATH', help='Path to JPEG image file')
    source_group.add_argument(
        '--generate',
        nargs='?',
        const='auto',
        metavar='[WxH]',
        help='Generate test image. Provide WxH (e.g., 320x240) or omit to auto-detect from /api/info'
    )
    source_group.add_argument('--dismiss', action='store_true', help='Dismiss currently displayed image')
    
    # Upload options
    parser.add_argument('--mode', choices=['full', 'strip'], default='full',
                       help='Upload mode: full (default, deferred decode) or strip (memory efficient)')
    parser.add_argument('--strip-height', type=int, default=16, metavar='N',
                       help='Strip height in pixels for strip mode (default: 16, min: 1, max: 240)')
    parser.add_argument('--quality', type=int, default=85, metavar='N',
                       help='JPEG quality 1-100 for generated/re-encoded images (default: 85)')
    parser.add_argument(
        '--seed',
        type=int,
        default=None,
        metavar='N',
        help='Seed for --generate (makes images vary deterministically; default: none)',
    )
    parser.add_argument(
        '--worst-case',
        action='store_true',
        help='For --generate: create a high-entropy noise image (less compressible; worst-case for upload size)',
    )
    parser.add_argument('--save', metavar='PATH', 
                       help='Save the final processed image to file before uploading (e.g., --save output.jpg)')
    parser.add_argument('--timeout', type=int, default=10, metavar='SECONDS',
                       help='Display timeout in seconds (default: 10, 0=forever)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Show detailed HTTP responses')
    
    args = parser.parse_args()

    # Host is required for all current operations.
    if not args.host:
        print_error("Host is required (device IP or mDNS hostname)")
        sys.exit(1)
    
    # Validate parameters
    if args.strip_height < 1 or args.strip_height > 1000:
        print_error("Strip height must be between 1 and 1000 pixels")
        sys.exit(1)
    
    if args.quality < 1 or args.quality > 100:
        print_error("Quality must be between 1 and 100")
        sys.exit(1)
    
    # Handle dismiss command
    if args.dismiss:
        print_header("Dismiss Image")
        success = dismiss_image(args.host, args.verbose)
        sys.exit(0 if success else 1)
    
    # Get JPEG data
    print_header("Image Upload")
    
    jpeg_image = None  # Keep PIL image for --save option
    
    if args.image:
        # Load from file
        image_path = Path(args.image)
        if not image_path.exists():
            print_error(f"Image file not found: {args.image}")
            sys.exit(1)
        
        if not image_path.suffix.lower() in ['.jpg', '.jpeg']:
            print_error("Image must be a JPEG file (.jpg or .jpeg)")
            sys.exit(1)
        
        print_info(f"Loading image: {image_path}")
        jpeg_data = image_path.read_bytes()
        print_success(f"Loaded {len(jpeg_data)} bytes")
        
    else:  # args.generate
        # Generate test image
        try:
            if args.generate is None:
                raise ValueError("Missing value for --generate")

            if args.generate == 'auto':
                width, height = get_device_display_coord_resolution(args.host, verbose=args.verbose)
                print_info(f"Auto-detected resolution from /api/info: {width}x{height}")
            else:
                width, height = parse_resolution(args.generate)

            if args.worst_case:
                jpeg_data = generate_worst_case_image(width, height, args.quality, seed=args.seed)
            else:
                jpeg_data = generate_test_image(width, height, args.quality, seed=args.seed)
            # Re-open to get PIL image for --save option
            if args.save:
                jpeg_image = Image.open(io.BytesIO(jpeg_data))
        except (ValueError, requests.exceptions.RequestException) as e:
            print_error(f"Failed to determine resolution: {e}")
            sys.exit(1)
    
    # Save image to file if requested (before upload)
    if args.save:
        save_path = Path(args.save)
        if jpeg_image is None:
            jpeg_image = Image.open(io.BytesIO(jpeg_data))
        jpeg_image.save(save_path, format='JPEG', quality=args.quality, optimize=True)
        print_success(f"Saved processed image to: {save_path}")
    
    # Upload image
    if args.mode == 'full':
        success = upload_full_image(args.host, jpeg_data, args.timeout, args.verbose)
    else:  # strip mode
        success = upload_strip_mode(args.host, jpeg_data, args.strip_height, args.timeout, args.quality, args.verbose)
    
    if success:
        print_header("Complete")
        print_success(f"Image displayed on {args.host}")
        if args.timeout > 0:
            print_info(f"Image will auto-dismiss after {args.timeout} seconds")
        sys.exit(0)
    else:
        print_header("Failed")
        sys.exit(1)


if __name__ == '__main__':
    main()
