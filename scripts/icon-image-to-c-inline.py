import sys

from PIL import Image

# We normally use an image in PNG format, 64x64, created with Gimp from the SVG image.
image_filename = sys.argv[1]

img = Image.open(image_filename).convert("RGBA")
img_data = list(img.getdata())

# Convert to C-style array
with open("icon.inl", "w") as f:
    f.write("static unsigned char icon_rgba[] = {\n")
    n = len(img_data)
    for i, pixel in enumerate(img_data):
        pre = "  " if i % 3 == 0 else ""
        suf = (",\n" if i % 3 == 2 else ", ") if i < n - 1 else "\n"
        f.write("%s0x%02x, 0x%02x, 0x%02x, 0x%02x%s" % (pre, *pixel, suf))
    f.write("};\n")
    f.write("static unsigned int icon_rgba_len = %d;" % (4 * n))

