#!/usr/bin/env python3
"""
Convert PNG image to RAW RGB565 format for ESP32-S3 display.
Usage: python3 png_to_raw.py input.png output.raw
"""

import sys
import os
from PIL import Image

def png_to_raw(input_file, output_file, target_width=240, target_height=320):
    """
    Convert PNG image to RAW RGB565 format.
    
    Args:
        input_file: Input PNG file path
        output_file: Output RAW file path
        target_width: Target width (default 240)
        target_height: Target height (default 320)
    """
    
    print(f"Converting {input_file} to {output_file}")
    print(f"Target size: {target_width}x{target_height}")
    
    try:
        # Open the image
        img = Image.open(input_file)
        print(f"Original size: {img.width}x{img.height}")
        print(f"Original mode: {img.mode}")
        
        # Convert to RGB if needed
        if img.mode != 'RGB':
            print(f"Converting from {img.mode} to RGB")
            img = img.convert('RGB')
        
        # Resize to target dimensions
        print(f"Resizing to {target_width}x{target_height}")
        img = img.resize((target_width, target_height), Image.Resampling.LANCZOS)
        
        # Convert to RGB565 and save as RAW
        print("Converting to RGB565...")
        pixels = img.load()
        
        with open(output_file, 'wb') as f:
            for y in range(target_height):
                for x in range(target_width):
                    r, g, b = pixels[x, y]
                    
                    # Convert to RGB565
                    # R: 5 bits (shift 11), G: 6 bits (shift 5), B: 5 bits
                    r5 = (r >> 3) & 0x1F
                    g6 = (g >> 2) & 0x3F
                    b5 = (b >> 3) & 0x1F
                    
                    color = (r5 << 11) | (g6 << 5) | b5
                    
                    # Write as big-endian (most significant byte first)
                    f.write(bytes([(color >> 8) & 0xFF, color & 0xFF]))
                
                # Progress indicator
                if y % 32 == 0:
                    print(f"  Progress: {y}/{target_height} rows")
        
        # Verify file size
        expected_size = target_width * target_height * 2  # 2 bytes per pixel
        actual_size = os.path.getsize(output_file)
        
        print(f"\nConversion complete!")
        print(f"Expected size: {expected_size} bytes ({expected_size/1024:.1f} KB)")
        print(f"Actual size: {actual_size} bytes ({actual_size/1024:.1f} KB)")
        
        if expected_size == actual_size:
            print("✓ File size is correct!")
        else:
            print(f"✗ Warning: File size mismatch!")
            print(f"  Difference: {abs(expected_size - actual_size)} bytes")
        
        return True
        
    except Exception as e:
        print(f"Error: {e}")
        return False

def batch_convert_pngs_in_directory(input_dir=".", output_dir=".", target_width=240, target_height=320):
    """
    Convert all PNG files in a directory to RAW format.
    """
    import glob
    
    print(f"Looking for PNG files in: {input_dir}")
    
    png_files = glob.glob(os.path.join(input_dir, "*.png"))
    if not png_files:
        print("No PNG files found!")
        return
    
    print(f"Found {len(png_files)} PNG file(s):")
    for png_file in png_files:
        print(f"  - {os.path.basename(png_file)}")
    
    print("\nStarting batch conversion...")
    
    success_count = 0
    for png_file in png_files:
        print(f"\n{'='*50}")
        base_name = os.path.splitext(os.path.basename(png_file))[0]
        raw_file = os.path.join(output_dir, f"{base_name}.raw")
        
        if png_to_raw(png_file, raw_file, target_width, target_height):
            success_count += 1
    
    print(f"\n{'='*50}")
    print(f"Batch conversion complete!")
    print(f"Successfully converted: {success_count}/{len(png_files)} files")

if __name__ == "__main__":
    # Check if PIL is installed
    try:
        from PIL import Image
    except ImportError:
        print("Error: Pillow library is not installed!")
        print("Install it with: pip install Pillow")
        sys.exit(1)
    
    # Parse command line arguments
    if len(sys.argv) == 3:
        # Single file conversion
        input_file = sys.argv[1]
        output_file = sys.argv[2]
        
        if not os.path.exists(input_file):
            print(f"Error: Input file '{input_file}' not found!")
            sys.exit(1)
        
        success = png_to_raw(input_file, output_file)
        sys.exit(0 if success else 1)
    
    elif len(sys.argv) == 2 and sys.argv[1] == "--batch":
        # Batch convert all PNGs in current directory
        batch_convert_pngs_in_directory()
    
    elif len(sys.argv) == 2 and sys.argv[1] == "--help":
        # Show help
        print("PNG to RAW RGB565 Converter")
        print("============================")
        print("Usage:")
        print("  python3 png_to_raw.py input.png output.raw")
        print("  python3 png_to_raw.py --batch (convert all PNGs in current directory)")
        print("  python3 png_to_raw.py --help (show this help)")
        print("\nOptions:")
        print("  The script automatically:")
        print("  1. Converts image to RGB mode")
        print("  2. Resizes to 240x320 pixels")
        print("  3. Converts to RGB565 format")
        print("  4. Saves as RAW binary file (153,600 bytes)")
    
    else:
        # Invalid arguments
        print("Invalid arguments!")
        print("Usage: python3 png_to_raw.py input.png output.raw")
        print("       python3 png_to_raw.py --batch")
        print("       python3 png_to_raw.py --help")
        sys.exit(1)
