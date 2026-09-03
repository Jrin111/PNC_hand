#!/usr/bin/env python3
"""Generate the uncalibrated PNC mounting templates from the shipped hand CAD.

Developer tool only: needs numpy, and Pillow when --preview-dir is used.
No ROS, hardware access, or network access is used.

The NanoSen August 2026 manual, p. 4, specifies 5 x 5 fingertip regions
and 22 palm regions. Its photograph shows palm rows 6/6/4/3/3, with the
short rows away from the thumb. The supplied 15 x 20 mm finger Gerbers
describe the five polygons below. Tracing their copper to U3 in
FlyingProbeTesting.json gives center/left/upper/right/lower = SIO 0/1/2/3/5.
SIO 4 routes to a separate reverse-side pad; it is not a sixth PNC region.

The original Inspire CAD does not include the added NanoSen material or
its carrier. Coordinates here are CAD-fitted mounting templates, NOT a
measured installation. Every real channel remains null. Demo chip order
and logical-channel/SIO equivalence are illustrative, pending wiring and
pipeline verification. Never promote demo_channel to channel implicitly.
"""

import argparse
import json
import math
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np


PACKAGE = Path(__file__).resolve().parents[1]
DESCRIPTION = PACKAGE.parents[1] / "robots" / "inspire_rh56e2_hand_description"
OFFSET = 0.0005
FINGERS = ("thumb", "index", "middle", "ring", "pinky")
# Coordinates in millimeters, as viewed from the unreflected Gerber top.
ELECTRODES = (
    ("center", 0, ((5, 5), (10, 5), (10, 15), (5, 15))),
    ("left", 1, ((0.5, 1), (4.5, 5), (4.5, 15), (0.5, 19))),
    ("upper", 2, ((5, 15.5), (10, 15.5), (14, 19.5), (1, 19.5))),
    ("right", 3, ((10.5, 5), (14.5, 1), (14.5, 19), (10.5, 15))),
    ("lower", 5, ((1, 0.5), (14, 0.5), (10, 4.5), (5, 4.5))),
)


def pose(element):
    result = np.eye(4)
    if element is None:
        return result
    roll, pitch, yaw = map(float, element.get("rpy", "0 0 0").split())
    sr, cr = math.sin(roll), math.cos(roll)
    sp, cp = math.sin(pitch), math.cos(pitch)
    sy, cy = math.sin(yaw), math.cos(yaw)
    result[:3, :3] = (
        (cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr),
        (sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr),
        (-sp, cp * sr, cp * cr),
    )
    result[:3, 3] = list(map(float, element.get("xyz", "0 0 0").split()))
    return result


def transform(points, matrix):
    return points @ matrix[:3, :3].T + matrix[:3, 3]


def read_stl(path):
    data = path.read_bytes()
    count = struct.unpack_from("<I", data, 80)[0]
    if len(data) != 84 + count * 50:
        raise ValueError(f"Expected binary STL: {path}")
    dtype = [("normal", "<f4", 3), ("vertices", "<f4", (3, 3)), ("attribute", "<u2")]
    return np.frombuffer(data[84:], dtype=dtype)["vertices"].astype(float)


class Hand:
    def __init__(self, side):
        self.side = side
        self.links = {}
        self.joints = {}
        self.transforms = {"base": np.eye(4)}
        root = ET.parse(DESCRIPTION / "urdf" / f"inspire_hand_e2_{side}_macro.xacro")
        for joint in root.iter("joint"):
            parent, child = joint.find("parent"), joint.find("child")
            if parent is None or child is None:
                continue
            try:
                origin = pose(joint.find("origin"))
            except ValueError:
                continue  # A separate generic xacro helper, not part of this hand.
            self.joints[child.get("link").replace("${prefix}", "")] = (
                parent.get("link").replace("${prefix}", ""), origin, joint.get("type")
            )
        for link in root.iter("link"):
            visual = link.find("visual")
            if visual is None:
                continue
            mesh = visual.find("geometry/mesh")
            if mesh is None:
                continue
            name = link.get("name").replace("${prefix}", "")
            path = DESCRIPTION / mesh.get("filename").split("inspire_rh56e2_hand_description/")[1]
            self.links[name] = transform(read_stl(path), pose(visual.find("origin")))

    def tf(self, name):
        if name not in self.transforms:
            parent, origin, _ = self.joints[name]
            self.transforms[name] = self.tf(parent) @ origin
        return self.transforms[name]

    def rigid_surface(self, frame):
        parent = self.joints[frame][0]
        names = [parent] + [name for name, (p, _, kind) in self.joints.items()
                            if p == parent and kind == "fixed"]
        inverse = np.linalg.inv(self.tf(frame))
        return np.concatenate([transform(self.links[name], inverse @ self.tf(name))
                               for name in names if name in self.links])

    def palm_surface(self):
        inverse = np.linalg.inv(self.tf("palm_1"))
        names = ("palm_1", "palm_2", "palm_force_sensor")
        return np.concatenate([transform(self.links[name], inverse @ self.tf(name))
                               for name in names])


class Surface:
    """Parallel rays in local u/v/n coordinates; no optional mesh libraries."""

    def __init__(self, triangles, basis):
        self.basis = np.asarray(basis, dtype=float)
        self.mesh = triangles @ self.basis
        a, b, c = self.mesh[:, 0], self.mesh[:, 1], self.mesh[:, 2]
        self.a, self.b, self.c = a, b, c
        self.den = (b[:, 1] - c[:, 1]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 1] - c[:, 1])
        self.valid = np.abs(self.den) > 1e-16
        self.den = np.where(self.valid, self.den, 1.0)

    def height(self, point):
        a, b, c = self.a, self.b, self.c
        u = ((b[:, 1] - c[:, 1]) * (point[0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (point[1] - c[:, 1])) / self.den
        v = ((c[:, 1] - a[:, 1]) * (point[0] - c[:, 0]) + (a[:, 0] - c[:, 0]) * (point[1] - c[:, 1])) / self.den
        w = 1.0 - u - v
        mask = self.valid & (u >= -1e-7) & (v >= -1e-7) & (w >= -1e-7)
        if not mask.any():
            return float("nan")
        return float(np.max((u * a[:, 2] + v * b[:, 2] + w * c[:, 2])[mask]))

    def fit(self, polygon):
        polygon = np.asarray(polygon, dtype=float)
        samples = sample_polygon(polygon)
        heights = np.array([self.height(point) for point in samples])
        if not np.isfinite(heights).all():
            raise ValueError("A patch extends beyond the existing CAD surface")
        design = np.column_stack((samples, np.ones(len(samples))))
        # Small decorative holes in the old CAD can expose the far wall, tens of
        # millimeters behind the mounting surface. A carrier bridges those holes;
        # they must not tilt its fitted front plane towards the back of the hand.
        front = heights >= np.quantile(heights, 0.60) - 0.0025
        coefficients = np.linalg.lstsq(design[front], heights[front], rcond=None)[0]
        # A local tangent plane, raised above all sampled shell/insert geometry.
        coefficients[2] += np.max(heights - design @ coefficients) + OFFSET
        depths = np.column_stack((polygon, np.ones(len(polygon)))) @ coefficients
        fitted = np.column_stack((polygon, depths)) @ self.basis.T
        gaps = (design @ coefficients - heights)[front]
        return fitted, float(np.max(gaps))


def sample_polygon(polygon):
    points = list(polygon)
    low, high = polygon.min(axis=0), polygon.max(axis=0)
    edges = np.roll(polygon, -1, axis=0) - polygon
    for x in np.linspace(low[0], high[0], 9):
        for y in np.linspace(low[1], high[1], 9):
            delta = np.array((x, y)) - polygon
            cross = edges[:, 0] * delta[:, 1] - edges[:, 1] * delta[:, 0]
            if np.all(cross >= -1e-12) or np.all(cross <= 1e-12):
                points.append((x, y))
    return np.unique(np.asarray(points), axis=0)


def zone(identifier, label, frame, polygon, demo_channel):
    return {
        "id": identifier, "label": label, "frame_id": frame,
        "polygon": np.round(polygon, 8).tolist(),
        "channel": None, "demo_channel": demo_channel, "gain": 1.0, "offset": 0.0,
    }


def generate(hand):
    zones, diagnostics = [], []
    # In palm_1: +X is the palm-facing surface. Left thumb lies at -Y.
    # Reflect the layout for the right-hand model instead of reusing left transforms.
    direction = 1 if hand.side == "left" else -1
    palm = Surface(hand.palm_surface(), np.array(((0, 0, 1), (1, 0, 0), (0, 1, 0))))
    index = 0
    for row, count in enumerate((6, 6, 4, 3, 3)):
        for column in range(count):
            center = np.array((direction * (0.0325 - column * 0.013), 0.105 - row * 0.013))
            if hand.side == "right":
                center[1] += 0.0005  # The shipped right palm CAD has a 0.5 mm datum shift.
            square = center + np.array(((-.006, -.006), (.006, -.006), (.006, .006), (-.006, .006)))
            polygon, gap = palm.fit(square)
            zones.append(zone(f"palm_{index + 1:02d}", f"Palm {index + 1:02d}", "palm_1", polygon,
                              f"raa{index // 6}_ch{index % 6}"))
            diagnostics.append((zones[-1]["id"], gap, 1.0))
            index += 1

    for number, finger in enumerate(FINGERS, start=4):
        frame = f"{finger}_force_sensor_{3 if finger == 'thumb' else 2}"
        basis = np.eye(3)
        if hand.side == "left" and finger == "index":
            # This particular STL has its thin axis along +X, not +Z.
            basis = np.array(((0, 0, 1), (0, 1, 0), (-1, 0, 0)))
        elif hand.side == "left" and finger == "thumb":
            # Thumb sensor_4 identifies the distal end at negative local Y.
            basis = np.diag((-1.0, -1.0, 1.0))
        surface = Surface(hand.rigid_surface(frame), basis)
        selected = None
        # Keep the electrode proportions. The old CAD is not the new sensor carrier;
        # a slightly smaller mounting template may be necessary at curved tip corners.
        for scale in (1.0, 0.96, 0.92, 0.88, 0.84):
            fitted = []
            try:
                for name, channel, coordinates in ELECTRODES:
                    local = (np.asarray(coordinates) - (7.5, 10.0)) * 0.001 * scale
                    polygon, gap = surface.fit(local)
                    fitted.append((name, channel, polygon, gap))
            except ValueError:
                continue
            selected = fitted
            break
        if selected is None:
            raise ValueError(f"Cannot fit a fingertip template for {hand.side}/{finger}")
        for name, channel, polygon, gap in selected:
            zones.append(zone(f"{finger}_{name}", f"{finger.title()} / {name}", frame, polygon,
                              f"raa{number}_ch{channel}"))
            diagnostics.append((zones[-1]["id"], gap, scale))

    assert len(zones) == 47
    assert len({item["id"] for item in zones}) == 47
    assert len({item["demo_channel"] for item in zones}) == 47
    assert all(item["frame_id"] in hand.links and item["channel"] is None for item in zones)
    data = {
        "schema_version": 1, "hand_side": hand.side, "mapping_verified": False,
        "provenance": (
            "基于 NanoSen 手册 p4 的 22+25 区布局及原始指尖 Gerber；"
            "CAD 为原 Inspire 外壳，新增 PNC 载体未建模。此配置是逐区射线拟合、"
            "距采样表面至少 0.5 mm 的装配位置模板，弯曲指尖必要时等比适配；"
            "实际安装坐标、左右/旋转方向和线序均未确认。"
            "demo_channel 仅示例：掌部按 22 槽排列，手指假定 raa4..8="
            "thumb/index/middle/ring/pinky；单板 SIO0/1/2/3/5 来自 Gerber 走线，"
            "硬件 logical channel 与 SIO 的 pipeline 关系仍需实测。channel 全部留空。"
        ),
        "zones": zones,
    }
    return data, diagnostics


def preview(hand, data, output):
    """Depth-buffered CAD preview in palm coordinates (no preview-only offsets)."""
    from PIL import Image, ImageDraw, ImageFont
    inverse = np.linalg.inv(hand.tf("palm_1"))
    all_triangles = []
    for name, triangles in hand.links.items():
        all_triangles.append(transform(triangles, inverse @ hand.tf(name)))
    triangles = np.concatenate(all_triangles)
    # A front-oblique view makes the palm surface offset and fingers visible together.
    view = np.array((1.0, 0.55 if hand.side == "left" else -0.55, 0.12))
    view /= np.linalg.norm(view)
    right = np.cross((0, 0, 1), view)
    right /= np.linalg.norm(right)
    up = np.cross(view, right)
    camera = np.column_stack((right, up, view))
    points = triangles @ camera
    low, high = points.reshape(-1, 3).min(0), points.reshape(-1, 3).max(0)
    width, height = 1400, 1400
    scale = min((width - 120) / (high[0] - low[0]), (height - 210) / (high[1] - low[1]))

    def pixels(poly):
        p = (poly[:, :2] - (low[:2] + high[:2]) / 2) * (scale, -scale)
        return p + (width / 2, height / 2 + 20)

    projected = pixels(points.reshape(-1, 3)).reshape(-1, 3, 2)
    normal = np.cross(points[:, 1] - points[:, 0], points[:, 2] - points[:, 0])
    length = np.linalg.norm(normal, axis=1)
    normal /= np.maximum(length[:, None], 1e-20)
    edge1, edge2 = projected[:, 1] - projected[:, 0], projected[:, 2] - projected[:, 0]
    area = np.abs(edge1[:, 0] * edge2[:, 1] - edge1[:, 1] * edge2[:, 0])
    keep = (area > 0.20) & (normal[:, 2] > 0)
    light = np.clip(0.55 + normal @ np.array((-.22, .35, .30)), .3, .96)
    items = [(points[i, :, 2], projected[i], tuple(int(c * light[i]) for c in (186, 194, 203)))
             for i in np.flatnonzero(keep)]
    colors = ((27, 167, 183), (45, 187, 145), (231, 185, 60), (239, 134, 62), (220, 82, 75))
    for index, item in enumerate(data["zones"]):
        polygon = transform(np.asarray(item["polygon"]), inverse @ hand.tf(item["frame_id"])) @ camera
        # Offset is already present in the actual config; no artificial preview lift.
        for i in range(1, len(polygon) - 1):
            triangle = polygon[[0, i, i + 1]]
            items.append((triangle[:, 2], pixels(triangle), colors[index % len(colors)]))
    rgb = np.full((height, width, 3), (247, 249, 252), dtype=np.uint8)
    depth = np.full((height, width), -np.inf, dtype=np.float32)
    for z, polygon, color in items:
        low_box = np.maximum(np.floor(polygon.min(axis=0)).astype(int), 0)
        high_box = np.minimum(np.ceil(polygon.max(axis=0)).astype(int), (width - 1, height - 1))
        x0, y0 = low_box
        x1, y1 = high_box
        if x1 < x0 or y1 < y0:
            continue
        a, b, c = polygon
        denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(denominator) < 1e-8:
            continue
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        u = ((b[1] - c[1]) * (xx + .5 - c[0]) + (c[0] - b[0]) * (yy + .5 - c[1])) / denominator
        v = ((c[1] - a[1]) * (xx + .5 - c[0]) + (a[0] - c[0]) * (yy + .5 - c[1])) / denominator
        w = 1 - u - v
        candidate = u * z[0] + v * z[1] + w * z[2]
        block = depth[y0:y1 + 1, x0:x1 + 1]
        mask = (u >= 0) & (v >= 0) & (w >= 0) & (candidate > block)
        block[mask] = candidate[mask]
        rgb[y0:y1 + 1, x0:x1 + 1][mask] = color
    image = Image.fromarray(rgb)
    draw = ImageDraw.Draw(image)
    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 30) if font_path.exists() else ImageFont.load_default()
    small = ImageFont.truetype(str(font_path), 22) if font_path.exists() else ImageFont.load_default()
    draw.text((width / 2, 40), f"{hand.side.title()} hand - 47-region CAD mounting template", anchor="mm", fill="#1b334b", font=font)
    draw.text((width / 2, height - 56), "Geometry and wiring unverified. Color differences illustrate separate zones only.", anchor="mm", fill="#52657b", font=small)
    image.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()
    (PACKAGE / "config").mkdir(exist_ok=True)
    if args.preview_dir:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
    for side in ("left", "right"):
        hand = Hand(side)
        data, diagnostics = generate(hand)
        target = PACKAGE / "config" / f"pnc_zones_{side}.json"
        target.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"{side}: 47 zones, real channels unset; max sampled plane gap {max(d[1] for d in diagnostics) * 1000:.2f} mm")
        print("  fingertip scales:", {name: scale for name, _, scale in diagnostics if name.endswith("_center")})
        if args.preview_dir:
            preview(hand, data, args.preview_dir / f"pnc_zones_{side}.png")


if __name__ == "__main__":
    main()
