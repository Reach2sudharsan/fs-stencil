
import struct
import subprocess
import sys
import unittest
from pathlib import Path


# locate the project root: the parent of the test/ directory
PROJECT_ROOT = Path(__file__).resolve().parents[1]
MAKER_PATH = PROJECT_ROOT / "fs-maker.py"
IMAGE_PATH = PROJECT_ROOT / "sys" / "empty-fs.iso"

# dilesystem layout
BLOCK_SIZE = 4096
NUM_INODES = 32
NUM_BLOCKS = 161
NUM_FREE_BLOCK_SLOTS = 30
NUM_DIRECT_BLOCK_SLOTS = 28

SUPERBLOCK_ID = 0
ROOT_INODE = 1
ROOT_DATA_BLOCK = 33
FIRST_FREE_INODE = 2

SV_MAGIC_NO = 134
SV_VERSION = 1

C_LONG = struct.Struct("@l")


def read_longs(image, block_number, count):
    offset = block_number * BLOCK_SIZE
    return [
        C_LONG.unpack_from(image, offset + i * C_LONG.size)[0]
        for i in range(count)
    ]


class TestFilesystemMaker(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        subprocess.run(
            [sys.executable, str(MAKER_PATH)],
            cwd=PROJECT_ROOT,
            check=True,
        )

        cls.image = IMAGE_PATH.read_bytes()
        assert len(cls.image) == NUM_BLOCKS * BLOCK_SIZE

    def test_superblock_fields_make_sense(self):
        fields = read_longs(self.image, SUPERBLOCK_ID, 36)

        magic = fields[0]
        first_free_inode = fields[1]
        nfree = fields[2]
        free_block_slots = fields[3:33]
        root_inode = fields[33]
        num_inodes = fields[34]
        version = fields[35]

        self.assertEqual(magic, SV_MAGIC_NO)
        self.assertEqual(first_free_inode, FIRST_FREE_INODE)
        self.assertEqual(nfree, NUM_FREE_BLOCK_SLOTS)
        self.assertEqual(len(free_block_slots), NUM_FREE_BLOCK_SLOTS)
        self.assertEqual(root_inode, ROOT_INODE)
        self.assertEqual(num_inodes, NUM_INODES)
        self.assertEqual(version, SV_VERSION)

        # these values reflect the current proposed overflow-list design
        self.assertEqual(free_block_slots[:29], list(range(34, 63)))
        self.assertEqual(free_block_slots[29], 63)

    def test_free_inode_chain(self):
        fields = read_longs(self.image, SUPERBLOCK_ID, 36)
        current_inode = fields[1]

        visited = []

        while current_inode != 0:
            self.assertTrue(FIRST_FREE_INODE <= current_inode <= NUM_INODES)
            self.assertNotIn(current_inode, visited)
            visited.append(current_inode)

            inode_fields = read_longs(self.image, current_inode, 3)
            next_free_inode = inode_fields[0]
            stored_inode_number = inode_fields[1]
            reference_count = inode_fields[2]

            self.assertEqual(stored_inode_number, current_inode)
            self.assertEqual(reference_count, 0)

            current_inode = next_free_inode

        self.assertEqual(
            visited,
            list(range(FIRST_FREE_INODE, NUM_INODES + 1)),
        )

    def test_free_data_blocks_and_overflow(self):
        super_fields = read_longs(self.image, SUPERBLOCK_ID, 36)
        free_block_slots = super_fields[3:33]

        ordinary_blocks = free_block_slots[:29]
        overflow_block = free_block_slots[29]

        # assumes the overflow block starts with a count, followed by block IDs
        overflow_fields = read_longs(
            self.image,
            overflow_block,
            BLOCK_SIZE // C_LONG.size,
        )

        overflow_count = overflow_fields[0]
        overflow_contents = overflow_fields[1:1 + overflow_count]

        self.assertGreater(overflow_count, 0)
        self.assertEqual(len(overflow_contents), overflow_count)

        all_free_blocks = (
            ordinary_blocks + [overflow_block] + overflow_contents
        )
        expected_free_blocks = list(range(ROOT_DATA_BLOCK + 1, NUM_BLOCKS))

        self.assertEqual(len(all_free_blocks), len(set(all_free_blocks)))
        self.assertEqual(sorted(all_free_blocks), expected_free_blocks)

        # check the particular layout currently implemented
        self.assertEqual(overflow_block, 63)
        self.assertEqual(overflow_contents, list(range(64, NUM_BLOCKS)))

    def test_root_inode_and_directory_entries(self):
        root_fields = read_longs(
            self.image,
            ROOT_INODE,
            3 + NUM_DIRECT_BLOCK_SLOTS + 1,
        )

        self.assertEqual(root_fields[0], BLOCK_SIZE)
        self.assertEqual(root_fields[1], ROOT_INODE)
        self.assertEqual(root_fields[2], 1)
        self.assertEqual(root_fields[3], ROOT_DATA_BLOCK)
        self.assertEqual(root_fields[-1], 0)

        # assumes directory entries are packed without struct padding
        entry_size = 2 * C_LONG.size + 25
        block_offset = ROOT_DATA_BLOCK * BLOCK_SIZE

        def read_entry(index):
            offset = block_offset + index * entry_size
            inode_number = C_LONG.unpack_from(self.image, offset)[0]
            next_offset = C_LONG.unpack_from(
                self.image, offset + C_LONG.size
            )[0]
            name_offset = offset + 2 * C_LONG.size
            raw_name = self.image[name_offset:name_offset + 25]
            name = raw_name.split(b"\x00", 1)[0].decode("ascii")
            return inode_number, next_offset, name

        dot = read_entry(0)
        dotdot = read_entry(1)

        self.assertEqual(dot, (ROOT_INODE, entry_size, "."))
        self.assertEqual(dotdot, (ROOT_INODE, 2 * entry_size, ".."))


if __name__ == "__main__":
    unittest.main()