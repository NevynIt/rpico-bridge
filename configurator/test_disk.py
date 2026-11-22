import unittest

from build_disk import (
    DATA_CLUSTERS,
    DATA_START,
    FAT12_MAX_CLUSTERS,
    FAT_COUNT,
    RESERVED_SECTORS,
    ROOT_ENTRIES,
    ROOT_SECTORS,
    SECTOR_SIZE,
    SECTORS_PER_FAT,
    TOTAL_SECTORS,
    build_image,
    extract_files,
    project_files,
)


class Fat12ImageTests(unittest.TestCase):
    def test_fixed_512_kib_geometry_is_fat12(self):
        self.assertEqual(TOTAL_SECTORS * SECTOR_SIZE, 512 * 1024)
        self.assertEqual(DATA_START, RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT + ROOT_SECTORS)
        self.assertEqual(DATA_CLUSTERS, 1003)
        self.assertLess(DATA_CLUSTERS, FAT12_MAX_CLUSTERS)
        self.assertEqual(ROOT_ENTRIES * 32, ROOT_SECTORS * SECTOR_SIZE)

    def test_image_is_deterministic_and_extracts_exact_files(self):
        files = [
            ("CONFIG.HTM", b"<html>offline app</html>"),
            ("README.TXT", b"quick start\n"),
            ("LICENSE.TXT", b"MIT License\n"),
        ]
        first = build_image(files)
        second = build_image(files)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 512 * 1024)
        self.assertEqual(extract_files(first), dict(files))

        drive_names = [name for name, _ in project_files(__file__)]
        self.assertEqual(drive_names, ["CONFIG.HTM", "README.TXT", "LICENSE.TXT"])

    def test_rejects_a_disk_full_of_file_content(self):
        with self.assertRaisesRegex(ValueError, "exceed"):
            build_image([("CONFIG.HTM", b"x" * (DATA_CLUSTERS * SECTOR_SIZE + 1))])


if __name__ == "__main__":
    unittest.main()
