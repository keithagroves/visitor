"""Contact sheet of a recorded shot: N evenly spaced frames, labelled with
their time, tiled 4 across.  python sheet.py shot.mp4 [N] [out.jpg]"""
import subprocess, sys, json, io
from PIL import Image, ImageDraw

src = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 8
out = sys.argv[3] if len(sys.argv) > 3 else src.rsplit('.', 1)[0] + '_sheet.jpg'
info = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                                           'format=duration', '-of', 'json', src]))
dur = float(info['format']['duration'])
W, H = 640, 360
tiles = []
for k in range(n):
    t = dur * (k + 0.5) / n
    png = subprocess.check_output(['ffmpeg', '-v', 'error', '-ss', f'{t:.3f}', '-i', src, '-frames:v', '1',
                                   '-vf', f'scale={W}:{H}', '-f', 'image2pipe', '-c:v', 'png', '-'])
    im = Image.open(io.BytesIO(png)).convert('RGB')
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 70, 18], fill=(0, 0, 0))
    d.text((4, 3), f'{t:5.2f}s', fill=(255, 255, 0))
    tiles.append(im)
cols = 4
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new('RGB', (W * cols, H * rows))
for k, im in enumerate(tiles):
    sheet.paste(im, ((k % cols) * W, (k // cols) * H))
sheet.save(out, quality=85)
print(out, f'{dur:.2f}s')
