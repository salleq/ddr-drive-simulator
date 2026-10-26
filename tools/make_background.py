#!/usr/bin/env python3
"""Generates assets/background.svg for DDR's "Retro" skin.

An 80s retrowave night scene: neon grid road, striped sunset, low-poly mountains and a
purple classic rear-engined sports coupe with round yellow headlights, seen from the
front-left and slightly above. The car is a generic design with no badges or names.

The car is a small lofted 3D model projected through a pinhole camera, so the perspective
holds together. Run:  python3 tools/make_background.py
"""
import math
import os
import random

W, H = 880, 712

# ---------------------------------------------------------------- camera
CAM = (6.8, 4.6, 2.3)          # metres; the car's footprint is centred on the origin
TARGET = (-0.2, -0.2, 0.55)
FOCAL = 900.0
CENTER = (W * 0.60, H * 0.60)


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


FWD = norm(sub(TARGET, CAM))
RIGHT = norm(cross(FWD, (0, 0, 1)))
UP = cross(RIGHT, FWD)


def project(p):
    v = sub(p, CAM)
    z = dot(v, FWD)
    return (CENTER[0] + dot(v, RIGHT) / z * FOCAL, CENTER[1] - dot(v, UP) / z * FOCAL), z


def pts(poly):
    return " ".join("%.1f,%.1f" % project(p)[0] for p in poly)


def centre3(poly):
    return tuple(sum(p[i] for p in poly) / len(poly) for i in range(3))


def normal(poly):
    n = (0.0, 0.0, 0.0)
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        n = add(n, cross(a, b))
    return norm(n)


def facing(poly):
    return dot(normal(poly), sub(CAM, centre3(poly))) > 0


def depth(poly):
    return dot(sub(centre3(poly), CAM), FWD)


LIGHT = norm((0.65, 0.55, 0.8))
RIM = norm((-0.8, -0.2, 0.3))   # pink rim light from behind


def lit(n, base, gloss=0.6, amb=0.38):
    d = max(0.0, dot(n, LIGHT))
    r = max(0.0, dot(n, RIM))
    view = norm(sub(CAM, (0, 0, 0.6)))
    h = norm(add(LIGHT, view))
    spec = max(0.0, dot(n, h)) ** 30 * gloss
    c = [base[i] * (amb + (1 - amb) * d) for i in range(3)]
    c = [c[0] + 255 * 0.35 * r, c[1] + 60 * 0.35 * r, c[2] + 200 * 0.35 * r]   # magenta rim
    c = [min(255, int(v + 255 * spec)) for v in c]
    return "#%02x%02x%02x" % tuple(c)


PURPLE = (120, 40, 210)
faces = []


def outward(poly):
    """Orients a face so that its normal points away from the car's centre line."""
    c = centre3(poly)
    if dot(normal(poly), sub(c, (c[0] * 0.85, 0.0, 0.55))) < 0:
        poly = list(reversed(poly))
    return poly


def quad(poly, fill, layer=0.0, stroke=None):
    poly = outward(poly)
    if not facing(poly):
        return
    s = stroke or fill
    faces.append((depth(poly) + layer,
                  f'<polygon points="{pts(poly)}" fill="{fill}" stroke="{s}" stroke-width="0.8" stroke-linejoin="round"/>'))


# ---------------------------------------------------------------- body: lofted sections
# x, half width, side top z, fender peak z, hood centre z
SECTIONS = [
    (2.16, 0.62, 0.50, 0.56, 0.52),
    (2.05, 0.74, 0.58, 0.70, 0.60),
    (1.80, 0.80, 0.64, 0.80, 0.64),
    (1.40, 0.82, 0.70, 0.82, 0.69),
    (0.95, 0.83, 0.76, 0.84, 0.76),
    (0.60, 0.83, 0.80, 0.84, 0.82),
    (0.00, 0.82, 0.84, 0.86, 0.86),
    (-0.70, 0.84, 0.86, 0.88, 0.88),
    (-1.20, 0.88, 0.86, 0.90, 0.88),
    (-1.70, 0.86, 0.80, 0.84, 0.80),
    (-2.00, 0.78, 0.70, 0.72, 0.70),
    (-2.18, 0.62, 0.58, 0.60, 0.58),
]
SILL = 0.26


def section_points(sec):
    x, w, side, fender, hood = sec
    # left half from the sill up and over to the centre line, then mirrored
    half = [
        (w * 0.96, SILL),
        (w * 1.00, SILL + (side - SILL) * 0.45),
        (w * 0.99, side - 0.03),
        (w * 0.93, side + (fender - side) * 0.55),
        (w * 0.76, fender),
        (w * 0.58, fender - (fender - hood) * 0.45),
        (w * 0.35, hood + 0.005),
        (0.0, hood),
    ]
    left = [(x, y, z) for y, z in half]
    right = [(x, -y, z) for y, z in reversed(half[:-1])]
    return left + right


rings = [section_points(s) for s in SECTIONS]
for i in range(len(rings) - 1):
    a, b = rings[i], rings[i + 1]
    for j in range(len(a) - 1):
        poly = outward([a[j], a[j + 1], b[j + 1], b[j]])
        quad(poly, lit(normal(poly), PURPLE))
# front and rear caps
front = rings[0]
quad(list(reversed(front)) + [(front[0][0], 0, SILL)], lit((1, 0, 0), PURPLE))
quad(rings[-1] + [(rings[-1][0][0], 0, SILL)], lit((-1, 0, 0), PURPLE))

# ---------------------------------------------------------------- cabin: windscreen, roof, fastback
# x, base half width, roof half width, roof z, base z
CABIN = [
    (0.62, 0.74, 0.70, 0.83, 0.83),
    (0.10, 0.72, 0.58, 1.27, 0.86),
    (-0.40, 0.72, 0.56, 1.31, 0.87),
    (-0.90, 0.74, 0.55, 1.25, 0.88),
    (-1.40, 0.76, 0.52, 1.10, 0.88),
    (-1.85, 0.74, 0.44, 0.92, 0.84),
    (-2.08, 0.66, 0.36, 0.76, 0.74),
]


def cabin_ring(c):
    x, bw, rw, rz, bz = c
    return [(x, bw, bz), (x, rw, rz), (x, -rw, rz), (x, -bw, bz)]


crings = [cabin_ring(c) for c in CABIN]
GLASS_FRONT = "url(#glass)"
for i in range(len(crings) - 1):
    a, b = crings[i], crings[i + 1]
    x0, x1 = CABIN[i][0], CABIN[i + 1][0]
    for j in range(3):
        poly = outward([a[j], a[j + 1], b[j + 1], b[j]])
        n = normal(poly)
        if j == 1:   # top strip: windscreen, roof, rear screen
            if x0 > 0.2:
                fill = "#3a2a6e"
            elif x1 < -1.3:
                fill = "#1c0d38"
            else:
                fill = lit(n, PURPLE)
        else:        # sides: glass with a thin body-coloured frame
            fill = "#2a1a52" if (x0 <= 0.62 and x1 >= -1.85) else lit(n, PURPLE)
        quad(poly, fill, layer=-0.02)

# window frames / pillars on the visible (left) side, drawn as neon-ish purple bands
side_frames = []
for (x, bw, rw, rz, bz) in CABIN[1:5]:
    if x in (0.10, -1.40):
        continue
pillar_b = [(-0.95, 0.735, 0.885), (-0.85, 0.735, 0.885), (-0.80, 0.555, 1.245), (-0.92, 0.55, 1.25)]
side_frames.append(f'<polygon points="{pts(pillar_b)}" fill="{lit(normal(pillar_b), PURPLE)}"/>')

# ---------------------------------------------------------------- lights and details
details = []
headlight_centres = []


def disc(c, n, r, k=24):
    n = norm(n)
    u = norm(cross(n, (0, 0, 1)))
    v = cross(n, u)
    return [add(c, add(mul(u, r * math.cos(2 * math.pi * i / k)), mul(v, r * math.sin(2 * math.pi * i / k)))) for i in range(k)]


for s in (1, -1):
    c = (2.02, s * 0.56, 0.665)
    n = (0.9, s * 0.15, 0.42)
    details.append(f'<polygon points="{pts(disc(c, n, 0.125))}" fill="#d9d2ec" stroke="#ffffff" stroke-width="1"/>')
    details.append(f'<polygon points="{pts(disc(add(c, mul(norm(n), 0.01)), n, 0.105))}" fill="#ffd21a"/>')
    details.append(f'<polygon points="{pts(disc(add(c, mul(norm(n), 0.02)), n, 0.06))}" fill="#fff6c8"/>')
    headlight_centres.append(project(c)[0])
    # amber indicators in the bumper
    ind = disc((2.15, s * 0.50, 0.42), (1, s * 0.2, 0.1), 0.04, 12)
    details.append(f'<polygon points="{pts(ind)}" fill="#ff9d1a"/>')

# slim chrome bumper
bump = [(2.20, 0.66, 0.37), (2.20, -0.66, 0.37), (2.20, -0.66, 0.31), (2.20, 0.66, 0.31)]
details.append(f'<polygon points="{pts(bump)}" fill="#cfc6e6" stroke="#ffffff" stroke-opacity="0.6"/>')
# air intake under the bumper
intake = [(2.17, 0.35, 0.29), (2.17, -0.35, 0.29), (2.15, -0.30, 0.22), (2.15, 0.30, 0.22)]
details.append(f'<polygon points="{pts(intake)}" fill="#12061f"/>')
# side mirror
mir = [(0.55, 0.80, 0.86), (0.55, 0.98, 0.90), (0.42, 0.98, 1.00), (0.40, 0.80, 0.97)]

# neon accent along the shoulder line (left side only, it faces the camera)
shoulder = [(s[0], s[1] * 0.99, s[2] - 0.03) for s in SECTIONS[1:-1]]
details.append(f'<polyline points="{pts(shoulder)}" fill="none" stroke="#ff4fd8" stroke-width="2" stroke-opacity="0.85"/>')
roof = [(c[0], c[2] * 0.0 + c[2] * 0 + c[2], c[3]) for c in CABIN]
roofline = [(c[0], c[2], c[3]) for c in CABIN]



# ---------------------------------------------------------------- wheels
def wheel(x, y, outward):
    out = []
    arch = disc((x, y + outward * 0.005, 0.33), (0, outward, 0), 0.41, 30)
    arch = [p for p in arch if p[2] >= SILL - 0.02]
    out.append(f'<polygon points="{pts(arch)}" fill="#0b0414"/>')
    out.append(f'<polygon points="{pts(disc((x, y + outward * 0.04, 0.32), (0, outward, 0), 0.32, 30))}" fill="#120c1c" stroke="#2c2142" stroke-width="2"/>')
    out.append(f'<polygon points="{pts(disc((x, y + outward * 0.07, 0.32), (0, outward, 0), 0.215, 30))}" fill="url(#rim)" stroke="#f0e8ff" stroke-width="1"/>')
    for k in range(5):
        a = 2 * math.pi * k / 5 + 0.4
        hole = disc((x + 0.13 * math.cos(a), y + outward * 0.075, 0.32 + 0.13 * math.sin(a)), (0, outward, 0), 0.045, 10)
        out.append(f'<polygon points="{pts(hole)}" fill="#3b2a5c"/>')
    out.append(f'<polygon points="{pts(disc((x, y + outward * 0.08, 0.32), (0, outward, 0), 0.05, 12))}" fill="#e6dcff"/>')
    return out


left_wheels = wheel(1.32, 0.82, 1) + wheel(-1.28, 0.88, 1)
right_front = f'<polygon points="{pts(disc((1.32, -0.82, 0.32), (0, -1, 0), 0.32, 30))}" fill="#0a0612"/>'


# ---------------------------------------------------------------- ground and sky
def g(x, y): return (x, y, 0.0)


horizon_y = project(g(-2000, 0))[0][1]


def clipped_line(a3, b3, near=0.8):
    """Clips a 3D segment against the camera's near plane and returns screen points, or None."""
    da, db = dot(sub(a3, CAM), FWD), dot(sub(b3, CAM), FWD)
    if da < near and db < near:
        return None
    if da < near:
        a3 = add(a3, mul(sub(b3, a3), (near - da) / (db - da)))
    if db < near:
        b3 = add(b3, mul(sub(a3, b3), (near - db) / (da - db)))
    return project(a3)[0], project(b3)[0]
ground_svg = []
for y in [i * 1.6 for i in range(-14, 15)]:
    seg = clipped_line(g(5.5, y), g(-300, y))
    if seg is None:
        continue
    a, b = seg
    ground_svg.append(f'<line x1="{a[0]:.1f}" y1="{a[1]:.1f}" x2="{b[0]:.1f}" y2="{b[1]:.1f}"/>')
for x in [5.5 - 1.6 * (1.25 ** i) for i in range(0, 22)]:
    seg = clipped_line(g(x, 30), g(x, -60))
    if seg is None:
        continue
    a, b = seg
    ground_svg.append(f'<line x1="{a[0]:.1f}" y1="{a[1]:.1f}" x2="{b[0]:.1f}" y2="{b[1]:.1f}"/>')

road = [g(5.5, 3.2), g(5.5, -3.2), g(-300, -3.2), g(-300, 3.2)]
edges = []
for y in (3.0, -3.0):
    e = [g(5.5, y + 0.06), g(5.5, y - 0.06), g(-300, y - 0.06), g(-300, y + 0.06)]
    edges.append(f'<polygon points="{pts(e)}" fill="#ff4fd8" opacity="0.8"/>')
for x0 in range(-80, 5, 4):
    d = [g(x0, 0.07 - 1.5), g(x0, -0.07 - 1.5), g(x0 + 2, -0.07 - 1.5), g(x0 + 2, 0.07 - 1.5)]
    edges.append(f'<polygon points="{pts(d)}" fill="#5ff4ff" opacity="0.7"/>')

beams = []
pool = [project(g(2.85 + 0.6 * math.cos(2 * math.pi * i / 32), 0.95 * math.sin(2 * math.pi * i / 32)))[0] for i in range(32)]
pool_c = project(g(2.85, 0))[0]
beams.append('<polygon points="%s" fill="url(#pool)"/>' % " ".join("%.1f,%.1f" % p for p in pool))
shadow = [g(2.4, 1.05), g(2.4, -1.05), g(-2.35, -1.05), g(-2.35, 1.05)]

# striped retro sun on the horizon, to the left of the car
sun_cx, sun_r = W * 0.30, 125
sun_cy = horizon_y - 55
stripes = []
for i in range(6):
    yy = sun_cy + 6 + i * 11
    hh = 2 + i * 1.5
    stripes.append(f'<rect x="{sun_cx - sun_r:.0f}" y="{yy:.1f}" width="{2 * sun_r}" height="{hh:.1f}" fill="#1d0a35"/>')

# low-poly mountains
random.seed(11)
mx = -20
peaks = [(mx, horizon_y)]
while mx < W + 40:
    mx += random.uniform(40, 90)
    peaks.append((mx, horizon_y - random.uniform(18, 70)))
    mx += random.uniform(30, 70)
    peaks.append((mx, horizon_y - random.uniform(0, 18)))
peaks.append((W + 40, horizon_y))
mountains = " ".join("%.1f,%.1f" % p for p in peaks)

stars = "".join(f'<circle cx="{random.uniform(0, W):.0f}" cy="{random.uniform(0, horizon_y * 0.75):.0f}" r="{random.choice((0.6, 0.8, 1.2))}" '
                f'fill="#ffffff" opacity="{random.uniform(0.25, 0.85):.2f}"/>' for _ in range(90))

glow_defs = "".join(
    f'<radialGradient id="glow{i}" gradientUnits="userSpaceOnUse" cx="{c[0]:.1f}" cy="{c[1]:.1f}" r="80">'
    '<stop offset="0" stop-color="#fff3a0" stop-opacity="0.9"/><stop offset="0.3" stop-color="#ffd21a" stop-opacity="0.35"/>'
    '<stop offset="1" stop-color="#ffb000" stop-opacity="0"/></radialGradient>' for i, c in enumerate(headlight_centres))
glows = "".join(f'<circle cx="{c[0]:.1f}" cy="{c[1]:.1f}" r="80" fill="url(#glow{i})"/>' for i, c in enumerate(headlight_centres))
pool_r = 110
shadow_c = project(g(0, 0))[0]
extra_defs = glow_defs + (
    f'<radialGradient id="pool" gradientUnits="userSpaceOnUse" cx="{pool_c[0]:.1f}" cy="{pool_c[1]:.1f}" r="{pool_r}">'
    '<stop offset="0" stop-color="#ffd21a" stop-opacity="0.26"/><stop offset="1" stop-color="#ffd21a" stop-opacity="0"/></radialGradient>'
    f'<radialGradient id="shadow" gradientUnits="userSpaceOnUse" cx="{shadow_c[0]:.1f}" cy="{shadow_c[1]:.1f}" r="230">'
    '<stop offset="0" stop-color="#000000" stop-opacity="0.8"/><stop offset="1" stop-color="#000000" stop-opacity="0"/></radialGradient>')

faces.sort(key=lambda f: -f[0])

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">
<!-- Generated by tools/make_background.py. Part of DDR, GNU AGPLv3. -->
<defs>
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#05030c"/>
    <stop offset="0.6" stop-color="#22093f"/>
    <stop offset="1" stop-color="#4a0d55"/>
  </linearGradient>
  <linearGradient id="sun" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffe45c"/>
    <stop offset="0.55" stop-color="#ff7a3d"/>
    <stop offset="1" stop-color="#ff2f8f"/>
  </linearGradient>
  <linearGradient id="floor" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#2a0838"/>
    <stop offset="1" stop-color="#07030d"/>
  </linearGradient>
  <linearGradient id="glass" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#2b1b55"/>
    <stop offset="0.42" stop-color="#7d5fc4"/>
    <stop offset="0.5" stop-color="#22134a"/>
    <stop offset="1" stop-color="#110826"/>
  </linearGradient>
  <linearGradient id="sideglass" x1="0" y1="0" x2="1" y2="0">
    <stop offset="0" stop-color="#170b2e"/>
    <stop offset="0.5" stop-color="#3d2a73"/>
    <stop offset="1" stop-color="#120824"/>
  </linearGradient>
  <radialGradient id="rim" cx="0.4" cy="0.35" r="0.7">
    <stop offset="0" stop-color="#f4eeff"/>
    <stop offset="1" stop-color="#7a6aa0"/>
  </radialGradient>
  <radialGradient id="lamp" cx="0.45" cy="0.4" r="0.6">
    <stop offset="0" stop-color="#fffbe0"/>
    <stop offset="0.6" stop-color="#ffd21a"/>
    <stop offset="1" stop-color="#e8a800"/>
  </radialGradient>
  <radialGradient id="glow" cx="0.5" cy="0.5" r="0.5">
    <stop offset="0" stop-color="#fff3a0" stop-opacity="0.9"/>
    <stop offset="0.3" stop-color="#ffd21a" stop-opacity="0.35"/>
    <stop offset="1" stop-color="#ffb000" stop-opacity="0"/>
  </radialGradient>
  <linearGradient id="beam" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#ffd21a" stop-opacity="0.30"/>
    <stop offset="1" stop-color="#ffd21a" stop-opacity="0"/>
  </linearGradient>
  {extra_defs}
  <radialGradient id="haze" cx="0.5" cy="0.5" r="0.5">
    <stop offset="0" stop-color="#ff3fb4" stop-opacity="0.45"/>
    <stop offset="1" stop-color="#ff3fb4" stop-opacity="0"/>
  </radialGradient>
</defs>
<rect width="{W}" height="{H}" fill="url(#sky)"/>
{stars}
<circle cx="{sun_cx:.0f}" cy="{sun_cy:.0f}" r="{sun_r}" fill="url(#sun)"/>
{''.join(stripes)}
<ellipse cx="{W * 0.45:.0f}" cy="{horizon_y:.0f}" rx="{W * 0.8:.0f}" ry="70" fill="url(#haze)"/>
<polygon points="{mountains}" fill="#12052a" stroke="#ff4fd8" stroke-opacity="0.6" stroke-width="1"/>
<rect x="0" y="{horizon_y:.1f}" width="{W}" height="{H - horizon_y:.1f}" fill="url(#floor)"/>
<g stroke="#b13cff" stroke-opacity="0.45" stroke-width="1">{''.join(ground_svg)}</g>
<polygon points="{pts(road)}" fill="#0c0617" opacity="0.85"/>
{''.join(edges)}
{''.join(beams)}
<polygon points="{pts(shadow)}" fill="url(#shadow)"/>
{right_front}
{''.join(f[1] for f in faces)}
{''.join(left_wheels)}
{''.join(details)}
{glows}
</svg>
'''

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "background.svg")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w") as f:
    f.write(svg)
print("wrote", os.path.normpath(out), "horizon y = %.0f" % horizon_y)
