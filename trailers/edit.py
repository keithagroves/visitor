#!/usr/bin/env python3
"""Cut a montage from its recorded shots, text cards and music.

    python edit.py 2026-11-17

Reads <dir>/montage.json: each segment takes `dur` seconds of build/shots/
<shot>.mp4 from `in` (negative = from the end), with its caption PNG (from
cards.mjs) sliding in at the bottom left and, for `card` segments, the
title/end card. Segments are hard cuts, on the beat of the music. The music
(music.py, arranged on the same timeline) is loudness-normalized to -14 LUFS.
Writes <dir>/build/<output>.
"""
import json
import os
import subprocess
import sys

D = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '.')
HERE = os.path.dirname(os.path.abspath(__file__))
spec = json.load(open(os.path.join(D, 'montage.json')))
B = os.path.join(D, 'build')
FPS = spec.get('fps', 60)
segs = spec['segments']
total = round(sum(s['dur'] for s in segs), 3)


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def duration(f):
    out = subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                                   '-of', 'default=nw=1:nk=1', f])
    return float(out)


# ---- music -------------------------------------------------------------------
m = spec['music']
raw = os.path.join(B, 'music.wav')
run([sys.executable, os.path.join(HERE, 'music.py'), raw, '--total', str(total),
     '--drop', str(m['drop']), '--b', str(m['b']), '--brk', str(m['break']),
     '--lift', str(m['lift']), '--end', str(m['end'])])
# two-pass loudnorm to -14 LUFS integrated, -1.5 dBTP
probe = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', raw, '-af',
                        'loudnorm=I=-14:TP=-1.5:LRA=11:print_format=json', '-f', 'null', '-'],
                       capture_output=True, text=True).stderr
stats = json.loads(probe[probe.rindex('{'):probe.rindex('}') + 1])
music = os.path.join(B, 'music_norm.wav')
run(['ffmpeg', '-y', '-v', 'error', '-i', raw, '-af',
     'loudnorm=I=-14:TP=-1.5:LRA=11:linear=true:'
     f"measured_I={stats['input_i']}:measured_TP={stats['input_tp']}:"
     f"measured_LRA={stats['input_lra']}:measured_thresh={stats['input_thresh']}:"
     f"offset={stats['target_offset']}",
     '-ar', '48000', music])

# ---- video -----------------------------------------------------------------------
inputs, chains, outs = [], [], []
k = 0
t0 = 0.0
for i, s in enumerate(segs):
    shot = os.path.join(B, 'shots', s['shot'] + '.mp4')
    L = duration(shot)
    t_in = s['in'] if s['in'] >= 0 else max(0.0, L + s['in'])
    dur = s['dur']
    inputs += ['-ss', f'{t_in:.3f}', '-i', shot]
    v = k
    k += 1
    # exact length: hold the last frame if the shot runs short
    chain = (f'[{v}:v]setpts=PTS-STARTPTS,fps={FPS},tpad=stop_mode=clone:stop_duration=2,'
             f'trim=duration={dur:.3f},setpts=PTS-STARTPTS,setsar=1')
    if s.get('push'):
        # a slow push-in (Ken Burns), done at 4K so the crop moves smoothly
        frames = int(round(dur * FPS))
        z = s['push'] - 1.0
        chain += (f",scale=3840:2160:flags=lanczos,zoompan=z='1+{z:.4f}*on/{frames}':"
                  f"x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=1:s=1920x1080:fps={FPS}")
    chain += f',format=yuv420p[b{i}]'
    chains.append(chain)
    cur = f'b{i}'
    n = str(i).zfill(2)
    if s.get('caption'):
        png = os.path.join(B, 'cards', f'{n}-caption.png')
        at = s.get('captionAt', 0.3)
        cd = s.get('captionDur', dur - 0.55)
        inputs += ['-loop', '1', '-framerate', str(FPS), '-t', f'{dur:.3f}', '-i', png]
        c = k
        k += 1
        chains.append(f'[{c}:v]format=rgba,fade=t=in:st={at:.3f}:d=0.4:alpha=1,'
                      f'fade=t=out:st={at + cd - 0.35:.3f}:d=0.35:alpha=1[c{i}]')
        # slide up 20 px into place as it fades in
        y = f"'20*pow(max(0,min(1,1-(t-{at:.3f})/0.55)),3)'"
        chains.append(f'[{cur}][c{i}]overlay=x=0:y={y}:format=auto:shortest=1[o{i}]')
        cur = f'o{i}'
    if s.get('card'):
        png = os.path.join(B, 'cards', f"{n}-{s['card']}.png")
        at = s.get('cardAt', 0.8)
        cd = s.get('cardDur', dur - at)
        inputs += ['-loop', '1', '-framerate', str(FPS), '-t', f'{dur:.3f}', '-i', png]
        c = k
        k += 1
        fade = f'format=rgba,fade=t=in:st={at:.3f}:d=0.9:alpha=1'
        if at + cd < dur - 0.05:
            fade += f',fade=t=out:st={at + cd - 0.8:.3f}:d=0.8:alpha=1'
        chains.append(f'[{c}:v]{fade}[t{i}]')
        chains.append(f'[{cur}][t{i}]overlay=0:0:format=auto:shortest=1[q{i}]')
        cur = f'q{i}'
    outs.append(f'[{cur}]')
    t0 += dur

chains.append(''.join(outs) + f'concat=n={len(segs)}:v=1:a=0,'
              f'fade=t=in:st=0:d=0.7,fade=t=out:st={total - 1.4:.3f}:d=1.4,format=yuv420p[v]')
inputs += ['-i', music]
a = k

graph = os.path.join(B, 'edit.filtergraph')
with open(graph, 'w') as f:
    f.write(';\n'.join(chains))

out = os.path.join(B, spec['output'])
run(['ffmpeg', '-y', '-v', 'error', '-stats', *inputs, '-filter_complex_script', graph,
     '-map', '[v]', '-map', f'{a}:a', '-t', f'{total:.3f}',
     '-c:v', 'libx264', '-preset', 'slow', '-crf', '17', '-tune', 'animation', '-profile:v', 'high',
     '-pix_fmt', 'yuv420p', '-r', str(FPS),
     '-c:a', 'aac', '-b:a', '256k', '-ar', '48000',
     '-movflags', '+faststart', out])
print(out, f'{total:.1f}s')
