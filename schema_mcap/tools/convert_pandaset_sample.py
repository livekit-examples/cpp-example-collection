#!/usr/bin/env python3
#
# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Convert PandaSet lidar frames into the schema_mcap sequence format.

The input is an official PandaSet scene directory containing lidar/*.pkl.gz.
The output has a text manifest and little-endian float32 frame files. Each
frame record is x, y, z, intensity. Only the generated fixture needs to be
present at runtime; this converter's pandas/numpy dependencies are optional.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Sequence


FORMAT_NAME = "schema_mcap_pointcloud_sequence"
FORMAT_VERSION = 1


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def discover_frames(scene_dir: Path) -> list[Path]:
    lidar_dir = scene_dir / "lidar"
    frames = sorted(lidar_dir.glob("*.pkl.gz"))
    if not frames:
        raise ValueError(f"no PandaSet lidar/*.pkl.gz frames found under {scene_dir}")
    return frames


def evenly_sample_indices(point_count: int, max_points: int) -> list[int]:
    if point_count <= max_points:
        return list(range(point_count))
    if max_points == 1:
        return [0]
    return [
        (index * (point_count - 1)) // (max_points - 1)
        for index in range(max_points)
    ]


def load_frame(
    path: Path, max_points: int, sensor_id: int, pose: dict[str, dict[str, float]]
) -> list[tuple[float, float, float, float]]:
    try:
        import numpy as np
        import pandas as pd
    except ImportError as error:
        raise RuntimeError(
            "regenerating the PandaSet fixture requires pandas; "
            "install it with `python3 -m pip install pandas`"
        ) from error

    frame = pd.read_pickle(path)
    required_columns = {"x", "y", "z", "i"}
    missing = required_columns.difference(frame.columns)
    if missing:
        raise ValueError(f"{path} is missing PandaSet columns: {sorted(missing)}")
    if "d" in frame.columns:
        frame = frame[frame["d"] == sensor_id]

    indices = evenly_sample_indices(len(frame), max_points)
    sampled = frame.iloc[indices]
    world_points = sampled[["x", "y", "z"]].to_numpy(dtype=np.float64)

    position = pose["position"]
    heading = pose["heading"]
    translation = np.array(
        [position["x"], position["y"], position["z"]], dtype=np.float64
    )
    w, x, y, z = (
        heading["w"],
        heading["x"],
        heading["y"],
        heading["z"],
    )
    rotation = np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ],
        dtype=np.float64,
    )
    ego_points = (world_points - translation) @ rotation
    intensities = sampled["i"].to_numpy(dtype=np.float64) / 255.0
    return [
        (float(point[0]), float(point[1]), float(point[2]), float(intensity))
        for point, intensity in zip(ego_points, intensities, strict=True)
    ]


def write_frame(path: Path, points: Sequence[tuple[float, float, float, float]]) -> None:
    with path.open("wb") as output:
        for point in points:
            output.write(struct.pack("<ffff", *point))


def convert(args: argparse.Namespace) -> None:
    scene_dir = args.scene_dir.resolve()
    output_dir = args.output_dir.resolve()
    source_frames = discover_frames(scene_dir)
    poses_path = scene_dir / "lidar" / "poses.json"
    if not poses_path.is_file():
        raise ValueError(f"PandaSet lidar poses not found: {poses_path}")
    poses = json.loads(poses_path.read_text(encoding="utf-8"))
    selected_frames = source_frames[args.start_frame : args.start_frame + args.frame_count]
    if not selected_frames:
        raise ValueError("the selected PandaSet frame range is empty")

    output_dir.mkdir(parents=True, exist_ok=True)
    frame_entries: list[dict[str, object]] = []
    selected_poses: list[dict[str, dict[str, float]]] = []
    for output_index, source_path in enumerate(selected_frames):
        source_index = int(source_path.name.split(".", maxsplit=1)[0])
        if source_index >= len(poses):
            raise ValueError(f"{source_path} has no matching entry in {poses_path}")
        pose = poses[source_index]
        points = load_frame(source_path, args.max_points, args.sensor_id, pose)
        if not points:
            raise ValueError(f"{source_path} contains no points for sensor {args.sensor_id}")
        output_name = f"frame_{output_index:03d}.bin"
        write_frame(output_dir / output_name, points)
        frame_entries.append(
            {
                "file": output_name,
                "points": len(points),
                "source_frame": source_path.name,
                "source_sha256": hashlib.sha256(source_path.read_bytes()).hexdigest(),
                "pose": pose,
            }
        )
        selected_poses.append(pose)
        print(f"wrote {output_name}: {len(points)} points from {source_path.name}")

    manifest = {
        "format": FORMAT_NAME,
        "version": FORMAT_VERSION,
        "frame_rate_hz": args.frame_rate,
        "point_layout": ["x", "y", "z", "intensity"],
        "point_encoding": "little-endian float32",
        "point_coordinate_system": "lidar ego frame",
        "source": {
            "dataset": "PandaSet",
            "scene": args.scene_id or scene_dir.name,
            "sensor_id": args.sensor_id,
            "license": "CC BY 4.0 with PandaSet Dataset Terms",
            "url": "https://pandaset.org/",
            "repository": args.source_repository,
            "revision": args.source_revision,
            "poses_file": "lidar/poses.json",
            "poses_sha256": hashlib.sha256(poses_path.read_bytes()).hexdigest(),
        },
        "frames": frame_entries,
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    manifest_lines = [
        f"# {FORMAT_NAME} v{FORMAT_VERSION}",
        f"# frame_rate_hz={args.frame_rate}",
        *(str(frame["file"]) for frame in frame_entries),
    ]
    (output_dir / "frames.txt").write_text(
        "\n".join(manifest_lines) + "\n", encoding="utf-8"
    )
    pose_lines = ["# x y z qx qy qz qw"]
    for pose in selected_poses:
        position = pose["position"]
        heading = pose["heading"]
        pose_lines.append(
            f"{position['x']:.17g} {position['y']:.17g} {position['z']:.17g} "
            f"{heading['x']:.17g} {heading['y']:.17g} "
            f"{heading['z']:.17g} {heading['w']:.17g}"
        )
    (output_dir / "poses.txt").write_text(
        "\n".join(pose_lines) + "\n", encoding="utf-8"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "scene_dir",
        type=Path,
        help="official PandaSet scene directory containing lidar/*.pkl.gz",
    )
    parser.add_argument("output_dir", type=Path, help="output sequence directory")
    parser.add_argument("--start-frame", type=int, default=0)
    parser.add_argument("--frame-count", type=positive_int, default=80)
    parser.add_argument("--max-points", type=positive_int, default=8192)
    parser.add_argument("--frame-rate", type=positive_int, default=10)
    parser.add_argument(
        "--scene-id",
        help="source scene identifier recorded in manifest.json (default: input directory name)",
    )
    parser.add_argument(
        "--source-repository",
        help="source repository URL recorded in manifest.json",
    )
    parser.add_argument(
        "--source-revision",
        help="source repository revision recorded in manifest.json",
    )
    parser.add_argument(
        "--sensor-id",
        type=int,
        default=0,
        help="PandaSet lidar sensor d value (default: 0, mechanical 360-degree lidar)",
    )
    return parser.parse_args()


if __name__ == "__main__":
    convert(parse_args())
