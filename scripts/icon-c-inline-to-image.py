import re
import sys

from PIL import Image

def parse_hex_array(file_path):
    # Read the C file
    with open(file_path, 'r') as f:
        content = f.read()
    
    # Extract hex values using regex
    hex_pattern = r'0x[0-9a-fA-F]{2}'
    hex_values = re.findall(hex_pattern, content)
    
    # Convert hex strings to integers
    bytes_data = [int(x, 16) for x in hex_values]
    return bytes_data

def create_image(bytes_data, width, height, output_path):
    # Convert bytes to RGBA image
    image = Image.frombytes('RGBA', (width, height), bytes(bytes_data))
    
    # Save the image
    image.save(output_path)
    print(f"Image saved to {output_path}")

def main():
    # Input file containing the C array
    input_file = sys.argv[1]
    
    # Output image file
    output_file = "output_image.png"  # Change this to your desired output file name
    
    # Image dimensions (you need to know these)
    width  = int(sys.argv[2])
    height = int(sys.argv[3])
    
    # Parse the hex array and create the image
    bytes_data = parse_hex_array(input_file)
    create_image(bytes_data, width, height, output_file)

if __name__ == "__main__":
    main()

