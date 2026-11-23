#!/usr/bin/env python3
"""An original soundtrack for a montage, synthesized from scratch (numpy only):
a soft synth pad, sub bass, plucked arpeggio and light drums at 100 BPM over a
IV-vi-V-I progression in C (Fmaj7 Am7 G6 Csus2, two bars each), arranged on
the montage's timeline so sections change on the cuts.

    python music.py out.wav --total 73.2 --drop 7.2 --b 33.6 --brk 54.0 --lift 58.8 --end 66.0

Times are seconds; at 100 BPM a bar is 2.4 s, so cuts on multiples of 1.2 s
land on the beat. Sections: intro (pad swelling, riser) until `drop`; groove
(bass, arp, kick, hats) until `b`; fuller groove (claps, brighter arp) until
`brk`; a breakdown (drums and bass out) until `lift`; peak until `end`; then
the drums stop and the last chord rings out under the end card, fading to
silence at `total`.
"""
import argparse
import numpy as np
import soundfile as sf

SR = 48000
BPM = 100.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
rng = np.random.default_rng(11)

PROG = [  # (bass root, pad voicing, arp notes) as MIDI numbers
    (41, [53, 57, 60, 64, 69], [65, 69, 72, 76]),   # Fmaj7
    (45, [52, 57, 60, 64, 67], [69, 72, 76, 79]),   # Am7
    (43, [50, 55, 59, 62, 64], [67, 71, 74, 76]),   # G6
    (36, [48, 55, 60, 62, 67], [72, 74, 79, 84]),   # Csus2
]
CHORD_BARS = 2


def mtof(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def secs(n):
    return np.arange(n) / SR


def fft_filter(x, lo=None, hi=None, order=2):
    """Zero-phase high/low-pass via the spectrum (x: (n,) or (n, 2))."""
    n = x.shape[0]
    X = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(n, 1.0 / SR)
    H = np.ones_like(f)
    if hi:
        H /= np.sqrt(1.0 + (f / hi) ** (2 * order))
    if lo:
        H /= np.sqrt(1.0 + (lo / np.maximum(f, 1e-3)) ** (2 * order))
    if x.ndim == 2:
        H = H[:, None]
    return np.fft.irfft(X * H, n=n, axis=0)


def add(buf, start_s, sig):
    """Mix a (n,2) or (n,) signal into the (N,2) buffer at a time."""
    i = int(round(start_s * SR))
    if i >= buf.shape[0]:
        return
    if sig.ndim == 1:
        sig = np.stack([sig, sig], axis=1)
    j = min(buf.shape[0], i + sig.shape[0])
    if i < 0:
        sig = sig[-i:]
        i = 0
    buf[i:j] += sig[: j - i]


def pan(sig, p):
    """p in -1 (left) .. 1 (right), equal power."""
    a = (p + 1) * np.pi / 4
    return np.stack([sig * np.cos(a), sig * np.sin(a)], axis=1)


# ---- instruments --------------------------------------------------------------

def saw_voice(f, n, bright, detune=(-6.0, 0.0, 6.0)):
    """Band-limited saw by partials; `bright` sets how fast they roll off."""
    t = secs(n)
    out = np.zeros((n, 2))
    for i, dc in enumerate(detune):
        ff = f * 2 ** (dc / 1200.0)
        s = np.zeros(n)
        for k in range(1, 40):
            if ff * k > 12000:
                break
            amp = np.exp(-(k - 1) / bright) / k
            if amp < 2e-3:
                break
            s += amp * np.sin(2 * np.pi * ff * k * t + rng.uniform(0, 2 * np.pi))
        p = (i - (len(detune) - 1) / 2) * 0.7
        out += pan(s, p)
    return out / len(detune)


def pad_chord(notes, dur, bright, attack, release):
    n = int((dur + release) * SR)
    t = secs(n)
    env = np.clip(t / attack, 0, 1)
    rel = np.where(t > dur, np.exp(-(t - dur) / (release / 4)), 1.0)
    env = env * rel
    sig = np.zeros((n, 2))
    for m in notes:
        sig += saw_voice(mtof(m), n, bright)
    # slow tremolo-free "breathing": a gentle swell through the chord
    swell = 0.85 + 0.15 * np.sin(np.pi * np.clip(t / max(dur, 1e-3), 0, 1))
    return sig * (env * swell)[:, None] / len(notes)


def pluck(f, bright, length=0.55):
    n = int(length * SR)
    t = secs(n)
    s = np.zeros(n)
    for k in range(1, 12):
        if f * k > 14000:
            break
        amp = 1.0 / k ** 1.3 * np.exp(-(k - 1) / bright)
        s += amp * np.exp(-t * (5.0 + 4.0 * k)) * np.sin(2 * np.pi * f * k * t)
    s *= np.clip(t / 0.004, 0, 1)
    return s


def bass_note(f, dur):
    n = int((dur + 0.3) * SR)
    t = secs(n)
    env = np.clip(t / 0.03, 0, 1) * np.where(t > dur, np.exp(-(t - dur) / 0.08), 1.0)
    s = np.sin(2 * np.pi * f * t) + 0.28 * np.sin(4 * np.pi * f * t) + 0.08 * np.sin(6 * np.pi * f * t)
    return s * env


def kick():
    n = int(0.42 * SR)
    t = secs(n)
    freq = 46 + 120 * np.exp(-t * 32)
    ph = 2 * np.pi * np.cumsum(freq) / SR
    s = np.sin(ph) * np.exp(-t * 7.5)
    click = rng.standard_normal(n) * np.exp(-t * 900) * 0.25
    return s + click


def hat(open_=False):
    n = int((0.25 if open_ else 0.07) * SR)
    t = secs(n)
    s = fft_filter(rng.standard_normal(n), lo=7000)
    return s * np.exp(-t * (14 if open_ else 70))


def clap():
    n = int(0.35 * SR)
    t = secs(n)
    noise = fft_filter(rng.standard_normal(n), lo=900, hi=5000)
    bursts = sum(np.exp(-np.maximum(t - d, 0) * 140) * (t >= d) for d in (0.0, 0.011, 0.023))
    tail = np.exp(-t * 16)
    return noise * (0.6 * bursts + 0.5 * tail)


def boom():
    n = int(3.0 * SR)
    t = secs(n)
    return np.sin(2 * np.pi * (38 + 14 * np.exp(-t * 3)) * t) * np.exp(-t * 1.3) * np.clip(t / 0.01, 0, 1)


def riser(dur):
    n = int(dur * SR)
    t = secs(n)
    x = t / dur
    noise = rng.standard_normal((n, 2))
    dark = fft_filter(noise, hi=900)
    bright = fft_filter(noise, lo=2500, hi=11000)
    sig = dark * (1 - x[:, None]) + bright * x[:, None]
    return sig * (x ** 2.2)[:, None]


def reverb_ir(rt60=2.6, pre=0.02):
    n = int((rt60 + pre) * SR)
    t = secs(n)
    ir = rng.standard_normal((n, 2)) * np.exp(-t * 6.9 / rt60)[:, None]
    ir[: int(pre * SR)] = 0
    ir = fft_filter(ir, hi=5500)
    return ir / np.sqrt(np.sum(ir ** 2) / 2)


def convolve(x, ir):
    n = x.shape[0] + ir.shape[0] - 1
    N = 1 << int(np.ceil(np.log2(n)))
    X = np.fft.rfft(x, N, axis=0)
    I = np.fft.rfft(ir, N, axis=0)
    return np.fft.irfft(X * I, N, axis=0)[: x.shape[0]]


# ---- arrangement ----------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--total', type=float, default=76.8)
    ap.add_argument('--drop', type=float, default=7.2)
    ap.add_argument('--b', type=float, default=36.0)
    ap.add_argument('--brk', type=float, default=None)
    ap.add_argument('--lift', type=float, default=62.4)
    ap.add_argument('--end', type=float, default=69.6)
    a = ap.parse_args()
    if a.brk is None:
        a.brk = a.lift
    E = 1e-6   # times are sums of 2.4 s bars: compare with a little slack
    before = lambda t, x: t < x - E
    in_break = lambda t: not before(t, a.brk) and before(t, a.lift)
    grooving = lambda t: not before(t, a.drop) and before(t, a.end) and not in_break(t)

    N = int((a.total + 0.5) * SR)
    pads = np.zeros((N, 2))
    bass = np.zeros((N, 2))
    arp = np.zeros((N, 2))
    drums = np.zeros((N, 2))
    fx = np.zeros((N, 2))
    kicks = []

    # Chords change every CHORD_BARS bars on a grid anchored at the drop, so
    # the drop lands on the first chord of the progression (the intro opens on
    # a partial chord if it isn't a whole number of chords long).
    chord_len = CHORD_BARS * BAR
    k = int(np.floor(-a.drop / chord_len + E))
    while True:
        t0 = a.drop + k * chord_len
        if not before(t0, a.total):
            break
        root, voicing, arpn = PROG[k % len(PROG)]
        start = max(t0, 0.0)
        final = not before(t0, a.end)
        if before(t0, a.drop):          # intro: dark, opening up
            bright = 1.6 + 2.4 * (t0 + chord_len) / max(a.drop, 1e-3)
            attack = 1.8
        elif not final:
            bright = 4.5 if before(t0, a.b) else 6.0
            attack = 0.35
        else:                            # the end card: the last chord, held
            bright = 3.5
            attack = 0.6
        dur = (t0 + chord_len - start) if not final else max(a.total - t0 - 2.5, chord_len)
        add(pads, start, pad_chord(voicing, dur, bright, attack, 2.5 if final else 1.6))
        if final:
            break

        # bass: the root on the beat (a pulse rather than a drone)
        for b in range(int(CHORD_BARS * 4)):
            tb = t0 + b * BEAT
            if not grooving(tb):
                continue
            add(bass, tb, bass_note(mtof(root), BEAT * 0.82) * 0.9)
            if b % 2 == 1:   # a little octave answer on the off-beat
                add(bass, tb + BEAT / 2, bass_note(mtof(root + 12), BEAT * 0.3) * 0.35)

        # arpeggio: sixteenths up and down the chord; sparse in the first
        # section and the breakdown, every step later
        pattern = [0, 1, 2, 3, 2, 1, 2, 3]
        for s in range(int(CHORD_BARS * 16)):
            ts = t0 + s * BEAT / 4
            if before(ts, a.drop) or not before(ts, a.end):
                continue
            if (before(ts, a.b) or in_break(ts)) and s % 2 == 1:
                continue
            m = arpn[pattern[s % len(pattern)]]
            vel = 0.75 + 0.25 * ((s % 4) == 0)
            br = 2.4 if before(ts, a.b) else 3.6
            p = pan(pluck(mtof(m), br) * vel, 0.35 if s % 2 else -0.35)
            add(arp, ts, p)
        k += 1

    # drums
    nbars = int(np.ceil(a.end / BAR + E))
    for bar in range(nbars):
        tb = bar * BAR
        if not grooving(tb):
            continue
        peak = not before(tb, a.lift)
        for kk in ([0, 2] if not peak else [0, 1, 2, 3]):
            kicks.append(tb + kk * BEAT)
            add(drums, tb + kk * BEAT, kick() * 0.95)
        if not before(tb, a.b):
            for k in (1, 3):
                add(drums, tb + k * BEAT, pan(clap() * 0.55, 0.05))
        steps = 16 if peak else 8
        for s in range(steps):
            ts = tb + s * BAR / steps
            off = (s % 2 == 1) if steps == 8 else (s % 4 == 2)
            vol = 0.20 if off else 0.08
            if steps == 16 and s % 2 == 1:
                vol = 0.06
            add(drums, ts, pan(hat(open_=(steps == 8 and s == 7)) * vol, 0.25))

    # transitions
    add(fx, a.drop - 2.4, riser(2.4) * 0.22)
    add(fx, a.lift - 1.2, riser(1.2) * 0.18)
    for tb in (a.drop, a.lift, a.end):
        add(fx, tb, boom() * 0.55)

    # sidechain: pads/bass/arp duck under each kick
    t = secs(N)
    duck = np.ones(N)
    for k in kicks:
        i = int(k * SR)
        n = min(N - i, int(0.35 * SR))
        if n > 0:
            duck[i:i + n] = np.minimum(duck[i:i + n], 1 - 0.55 * np.exp(-secs(n) * 9))
    duck = duck[:, None]

    ir = reverb_ir()
    music = pads * 0.55 + arp * 0.16
    wet = convolve(music + drums * 0.08 + fx * 0.3, ir)
    mix = (pads * 0.55 * duck + bass * 0.30 * duck + arp * 0.16 * duck + drums * 0.42 + fx * 0.7
           + wet * 0.24)
    mix = fft_filter(mix, lo=28)

    # fade out at the very end
    fade = np.clip((a.total - t) / 3.0, 0, 1)[:, None]
    mix *= fade
    mix = mix[: int(a.total * SR)]
    # gentle glue: soft clip, then peak-normalize to -1 dBFS
    mix /= np.max(np.abs(mix)) + 1e-9
    mix = np.tanh(mix * 1.6) / np.tanh(1.6)
    mix *= 10 ** (-1.0 / 20) / (np.max(np.abs(mix)) + 1e-9)
    sf.write(a.out, mix.astype(np.float32), SR, subtype='PCM_24')
    print(a.out, f'{mix.shape[0] / SR:.2f}s')


if __name__ == '__main__':
    main()
