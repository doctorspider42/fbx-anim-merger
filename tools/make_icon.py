#!/usr/bin/env python3
"""
Draws the application icon and writes assets/icon.ico plus the PNG the README
uses. The icon is generated rather than hand-drawn so it stays reproducible and
reviewable: this file is the source, the binaries next to it are build output
that happens to be committed (Windows resource compilation needs a real .ico).

    python tools/make_icon.py

The subject is a run cycle - two ghosted poses trailing one solid figure, drawn
as a joint rig rather than a silhouette, which is exactly what the application
does: several animation takes merged onto one skeleton.

Sizes below 48px drop the ghosts and thicken the strokes; a three-figure scene
turns to mush at 16px, and every shipped Windows surface asks for that size.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# Everything is drawn at this resolution in normalised (0..1) coordinates and
# downsampled from there, so the strokes get their antialiasing for free.
MASTER = 2048

# ICO members. Windows picks the nearest and rescales the rest; shipping the
# in-between sizes keeps the taskbar and the Explorer detail views crisp.
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)

# The badge. Deep indigo falling to near-black violet along the diagonal.
BG_TOP = (58, 40, 132)
BG_BOTTOM = (14, 10, 34)
GLOW = (124, 92, 255)

# The trail, oldest first. Alpha carries the fade; the hue shift from violet to
# cyan gives the three passes a direction even where they overlap.
GHOSTS = (
    ((150, 122, 255), 0.30),
    ((64, 190, 255), 0.50),
)
FIGURE = (255, 255, 255)
FIGURE_FOOT = (196, 234, 255)
FIGURE_GLOW = (110, 220, 255)


# ---------------------------------------------------------------------------
# Poses
#
# One run cycle, sampled three times. Coordinates are normalised to the badge,
# y downwards, the figure facing right. Hand-placed rather than solved from
# angles: three poses are quicker to nudge by eye than a cycle to tune.
# ---------------------------------------------------------------------------
def pose(head, neck, chest, pelvis, arm_front, arm_back, leg_front, leg_back):
    return {
        "head": head,
        "neck": neck,
        "chest": chest,
        "pelvis": pelvis,
        "arm_front": arm_front,
        "arm_back": arm_back,
        "leg_front": leg_front,
        "leg_back": leg_back,
    }


# Contact: the front foot lands, the stride at its widest.
POSE_A = pose(
    head=(0.522, 0.290),
    neck=(0.508, 0.342),
    chest=(0.498, 0.394),
    pelvis=(0.468, 0.528),
    arm_front=((0.556, 0.462), (0.618, 0.418)),
    arm_back=((0.408, 0.458), (0.344, 0.416)),
    leg_front=((0.544, 0.638), (0.596, 0.744)),
    leg_back=((0.406, 0.650), (0.344, 0.744)),
)

# Passing: the legs cross under the hips, the arms swap.
POSE_B = pose(
    head=(0.528, 0.284),
    neck=(0.514, 0.336),
    chest=(0.503, 0.388),
    pelvis=(0.469, 0.524),
    arm_front=((0.566, 0.456), (0.630, 0.406)),
    arm_back=((0.424, 0.452), (0.366, 0.400)),
    leg_front=((0.528, 0.612), (0.548, 0.720)),
    leg_back=((0.420, 0.640), (0.352, 0.726)),
)

# Drive: knee up with the heel tucked under it, back leg straight off the toe,
# torso leaning into the next step. This is the pose the eye lands on.
POSE_C = pose(
    head=(0.534, 0.278),
    neck=(0.520, 0.330),
    chest=(0.508, 0.382),
    pelvis=(0.470, 0.520),
    arm_front=((0.576, 0.452), (0.640, 0.396)),
    arm_back=((0.440, 0.448), (0.386, 0.386)),
    leg_front=((0.556, 0.596), (0.520, 0.672)),
    leg_back=((0.404, 0.628), (0.330, 0.712)),
)

HEAD_RADIUS = 0.040

# The foot, continuing forward from the ankle. Without one the legs stop dead
# and the whole thing reads as a diagram rather than a figure.
FOOT = (0.036, 0.012)


# ---------------------------------------------------------------------------
# Drawing primitives
# ---------------------------------------------------------------------------
def _px(point, size):
    return (point[0] * size, point[1] * size)


def _capsule(draw, a, b, width, color, size):
    """A bone: a line with round caps, which ImageDraw has no flag for."""
    ax, ay = _px(a, size)
    bx, by = _px(b, size)
    half = width * size / 2
    draw.line((ax, ay, bx, by), fill=color, width=max(1, int(round(width * size))))
    for x, y in ((ax, ay), (bx, by)):
        draw.ellipse((x - half, y - half, x + half, y + half), fill=color)


def _dot(draw, point, radius, color, size):
    x, y = _px(point, size)
    r = radius * size
    draw.ellipse((x - r, y - r, x + r, y + r), fill=color)


def draw_figure(size, joints, bone, color, joint_scale=1.0, offset=(0.0, 0.0), scale=1.0):
    """
    Renders one pose opaque on its own layer. Compositing whole layers rather
    than overlapping strokes is what keeps the joints from showing through the
    bones as darker blobs once the alpha is applied.
    """
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    pivot = joints["pelvis"]

    def place(point):
        return (
            pivot[0] + (point[0] - pivot[0]) * scale + offset[0],
            pivot[1] + (point[1] - pivot[1]) * scale + offset[1],
        )

    chest = place(joints["chest"])
    pelvis = place(joints["pelvis"])
    neck = place(joints["neck"])
    head = place(joints["head"])

    limbs = []
    for key, root in (
        ("arm_back", chest),
        ("leg_back", pelvis),
        ("arm_front", chest),
        ("leg_front", pelvis),
    ):
        mid, end = (place(p) for p in joints[key])
        limbs.append((key, root, mid, end))

    # Back limbs first so the near side reads as nearer.
    for key, root, mid, end in limbs:
        _capsule(draw, root, mid, bone, color, size)
        _capsule(draw, mid, end, bone * 0.92, color, size)
        if key.startswith("leg"):
            toe = (end[0] + FOOT[0] * scale, end[1] + FOOT[1] * scale)
            _capsule(draw, end, toe, bone * 0.82, color, size)

    _capsule(draw, pelvis, chest, bone * 1.24, color, size)
    _capsule(draw, chest, neck, bone * 1.08, color, size)

    # Joints are drawn a good deal fatter than the bones they connect: that
    # contrast is what makes the figure read as a rig rather than a stick man.
    for _, _, mid, _ in limbs:
        _dot(draw, mid, bone * 0.78 * joint_scale, color, size)
    for point in (chest, pelvis):
        _dot(draw, point, bone * 0.88 * joint_scale, color, size)

    _dot(draw, head, HEAD_RADIUS * scale, color, size)
    return layer


def with_alpha(layer, alpha):
    r, g, b, a = layer.split()
    return Image.merge("RGBA", (r, g, b, a.point(lambda v: int(v * alpha))))


def tinted(layer, color):
    solid = Image.new("RGBA", layer.size, color + (255,))
    solid.putalpha(layer.split()[3])
    return solid


def vertical_tint(layer, top, bottom):
    """
    Grades the figure from head to foot across its own bounds. Flat white reads
    as a cut-out; a few degrees of cool at the feet gives it some depth without
    turning the hero into a second coloured pass.
    """
    alpha = layer.split()[3]
    _, y0, _, y1 = alpha.getbbox()
    size = layer.size[0]
    t = np.clip((np.arange(size, dtype=np.float32) - y0) / max(1, y1 - y0), 0.0, 1.0)
    ramp = np.array(top, np.float32) + (np.array(bottom, np.float32) - np.array(top, np.float32)) * t[:, None]
    rgb = np.repeat(ramp[:, None, :], size, axis=1).astype(np.uint8)
    graded = Image.fromarray(rgb).convert("RGBA")
    graded.putalpha(alpha)
    return graded


# ---------------------------------------------------------------------------
# The badge
# ---------------------------------------------------------------------------
def rounded_mask(size, radius):
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255)
    return mask


def diagonal_gradient(size, top, bottom):
    """Top-left to bottom-right, eased so the mid tones do not band."""
    ramp = np.linspace(0.0, 1.0, size, dtype=np.float32)
    t = (ramp[None, :] + ramp[:, None]) / 2.0
    t = t * t * (3.0 - 2.0 * t)
    top_arr = np.array(top, dtype=np.float32)
    bottom_arr = np.array(bottom, dtype=np.float32)
    rgb = top_arr + (bottom_arr - top_arr) * t[..., None]
    return Image.fromarray(np.dstack([rgb.astype(np.uint8), np.full((size, size), 255, np.uint8)]))


def radial_glow(size, center, radius, color, strength):
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32) / size
    d = np.hypot(xs - center[0], ys - center[1]) / radius
    falloff = np.clip(1.0 - d, 0.0, 1.0) ** 2.2
    alpha = (falloff * strength * 255).astype(np.uint8)
    glow = Image.new("RGBA", (size, size), color + (0,))
    glow.putalpha(Image.fromarray(alpha))
    return glow


def fit(scene, bbox, extent, center_y):
    """
    Scales `scene` so that `bbox` - the subject's real bounds within it - spans
    `extent` of the badge, then centres it. The transform is applied to the whole
    canvas, which keeps anything sitting outside the measured bounds (the halo)
    in register with the figure.
    """
    size = scene.size[0]
    left, top, right, bottom = bbox
    factor = extent * size / max(right - left, bottom - top)

    scaled = scene.resize((round(size * factor),) * 2, Image.LANCZOS)
    placed = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    placed.alpha_composite(
        scaled,
        (
            round(size * 0.5 - (left + right) / 2 * factor),
            round(size * center_y - (top + bottom) / 2 * factor),
        ),
    )
    return placed


def render(size, detailed):
    """
    `detailed` draws the full three-pose scene. Without it the trail is dropped
    and the remaining figure is drawn larger and heavier, which is the only way
    the subject survives a 16px box.
    """
    radius = size * 0.225
    mask = rounded_mask(size, radius)

    badge = diagonal_gradient(size, BG_TOP, BG_BOTTOM)
    badge.alpha_composite(radial_glow(size, (0.44, 0.46), 0.62, GLOW, 0.40))
    badge.alpha_composite(radial_glow(size, (0.18, 0.10), 0.55, (150, 130, 255), 0.20))

    bone = 0.029 if detailed else 0.050
    scene = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    geometry = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    if detailed:
        # The trail runs down and to the left, not straight back: a diagonal
        # fills a square badge where a row of three only fills a band across it.
        trail = ((POSE_A, (-0.216, 0.096), 0.85), (POSE_B, (-0.108, 0.048), 0.93))
        for (joints, offset, shrink), (color, alpha) in zip(trail, GHOSTS):
            layer = draw_figure(size, joints, bone * 0.94, color + (255,), offset=offset, scale=shrink)
            scene.alpha_composite(with_alpha(layer, alpha))
            geometry.alpha_composite(layer)

    hero = draw_figure(size, POSE_C, bone, FIGURE + (255,), scale=1.0 if detailed else 1.06)
    hero = vertical_tint(hero, FIGURE, FIGURE_FOOT)

    # A halo under the hero: it lifts the white off the violet without an
    # outline, which at 16px would only close up the gaps between the bones.
    halo = tinted(hero, FIGURE_GLOW).filter(ImageFilter.GaussianBlur(size * 0.020))
    scene.alpha_composite(with_alpha(halo, 0.55))
    scene.alpha_composite(hero)
    geometry.alpha_composite(hero)

    # Fit the scene to the badge from its own bounds rather than trusting the
    # coordinates above to stay centred - they get nudged by eye, and a pose
    # edit should not quietly shift the whole composition off-centre. `geometry`
    # is measured instead of `scene` so the halo's blur is not counted as part
    # of the subject. Slightly above the middle: optical centre, not maths.
    badge.alpha_composite(fit(scene, geometry.getbbox(), 0.76 if detailed else 0.72, 0.485))

    # Glass edge: a bright rim along the top, fading out by the equator.
    rim = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle(
        (0, 0, size - 1, size - 1),
        radius=radius,
        outline=(255, 255, 255, 96),
        width=max(1, int(size * 0.008)),
    )
    fade = np.clip(1.0 - np.linspace(0.0, 1.0, size, dtype=np.float32) * 1.7, 0.0, 1.0)
    rim.putalpha(Image.fromarray((np.array(rim.split()[3], np.float32) * fade[:, None]).astype(np.uint8)))
    badge.alpha_composite(rim)

    badge.putalpha(mask)
    return badge


def resize(master, size):
    return master.resize((size, size), Image.LANCZOS)


def main():
    root = Path(__file__).resolve().parent.parent
    assets = root / "assets"
    assets.mkdir(exist_ok=True)

    detailed = render(MASTER, detailed=True)
    compact = render(MASTER, detailed=False)

    # Pillow stores 256px ICO members as PNG and the rest as BMP by itself.
    frames = [resize(detailed if size >= 48 else compact, size) for size in ICO_SIZES]
    frames[-1].save(
        assets / "icon.ico",
        format="ICO",
        sizes=[(s, s) for s in ICO_SIZES],
        append_images=frames[:-1],
    )

    resize(detailed, 512).save(assets / "icon.png")
    print(f"wrote {assets / 'icon.ico'} ({', '.join(str(s) for s in ICO_SIZES)})")


if __name__ == "__main__":
    main()
