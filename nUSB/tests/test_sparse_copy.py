#!/usr/bin/env python3
import errno
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from run_large_tests import sparse_copy


class SparseCopyTest(unittest.TestCase):
    def copy_fixture(self, fallback=False):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            destination = Path(directory) / "copy.img"
            with source.open("wb", buffering=0) as stream:
                stream.truncate(4 * 1024 * 1024)
                for offset, count in ((0, 512), (16384, 4608),
                                      (65536, 4096), (2162688, 135168),
                                      (3145728, 8192)):
                    stream.seek(offset)
                    stream.write(bytes((i % 251) + 1 for i in range(count)))
            if fallback:
                with patch("run_large_tests.os.lseek",
                           side_effect=OSError(errno.EINVAL, "no extent seeks")):
                    sparse_copy(source, destination)
            else:
                sparse_copy(source, destination)
            self.assertEqual(destination.stat().st_size, source.stat().st_size)
            self.assertEqual(destination.read_bytes(), source.read_bytes())

    def test_extent_copy(self):
        self.copy_fixture()

    def test_scan_copy(self):
        self.copy_fixture(fallback=True)


if __name__ == "__main__":
    unittest.main()
