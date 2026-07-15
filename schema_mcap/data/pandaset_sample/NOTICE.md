# PandaSet Sample Notice

The binary point-cloud frames in this directory are derived from all 80 frames
(00 through 79) of PandaSet scene 001, mechanical lidar sensor 0.

PandaSet was created by Scale AI, Inc. and Hesai Photonics Technology Co.,
Ltd. The dataset is provided under the
[Creative Commons Attribution 4.0 International license](https://creativecommons.org/licenses/by/4.0/)
and the [PandaSet Dataset Terms of Use](http://scalesd.org/pandaset-terms-of-use.html).
The terms distributed with the source data are preserved in
`PANDASET_TERMS.txt`. This sample remains subject to those terms and is not
endorsed by or affiliated with Scale AI or Hesai.

The source frames were modified as follows:

- selected the mechanical 360-degree lidar (`d == 0`);
- evenly sampled each scan to 8192 points;
- transformed world-frame points into the lidar-local frame using the inverse
  per-frame pose from `lidar/poses.json`;
- normalized intensity from the PandaSet 0–255 range to 0–1; and
- repacked each point as little-endian `float32` values in
  `x, y, z, intensity` order.

The original world-frame positions and quaternions are retained in `poses.txt`
and are published as synchronized `map` → `lidar` transforms.

The source frames are commit-pinned in the public
[`autoexpert-cvpr2026-workshop/seq`](https://huggingface.co/datasets/autoexpert-cvpr2026-workshop/seq)
repository at revision `7af0c275d38291e4cbfe9481439c93bc48e01f3d`.
See `manifest.json` for each original filename, source pose, and SHA-256 hash.
The sample can be regenerated from an official PandaSet scene download with:

```sh
python3 schema_mcap/tools/convert_pandaset_sample.py \
  /path/to/pandaset/001 \
  schema_mcap/data/pandaset_sample \
  --scene-id 001 \
  --frame-count 80 \
  --max-points 8192 \
  --source-repository https://huggingface.co/datasets/autoexpert-cvpr2026-workshop/seq \
  --source-revision 7af0c275d38291e4cbfe9481439c93bc48e01f3d
```

When using PandaSet, cite:

> P. Xiao, Z. Shao, S. Hao, Z. Zhang, X. Chai, J. Jiao, Z. Li, J. Wu,
> K. Sun, K. Jiang, Y. Wang, and D. Yang, “PandaSet: Advanced Sensor Suite
> Dataset for Autonomous Driving,” 2021 IEEE International Intelligent
> Transportation Systems Conference (ITSC), pp. 3095–3101.
