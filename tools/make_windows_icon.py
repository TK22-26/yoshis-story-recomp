"""Wrap the approved project PNG in an ICO for the Windows resource compiler."""
import argparse
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
png = args.source.read_bytes()
if png[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", png[16:24]) != (256, 256):
    raise SystemExit("Expected the approved 256 x 256 project icon")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(struct.pack("<HHH", 0, 1, 1) +
                       struct.pack("<BBBBHHII", 0, 0, 0, 0, 1, 32, len(png), 22) + png)
