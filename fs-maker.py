
from pathlib import Path
import struct

# sample comment

# disk layout
BLOCK_SIZE = 4096

NUM_INODES = 32
NUM_DATA_BLOCKS = 128
NUM_BLOCKS = 1 + NUM_INODES + NUM_DATA_BLOCKS

SUPERBLOCK_ID = 0
INODE_START_BLOCK = 1
DATA_BLOCK_START_BLOCK = INODE_START_BLOCK + NUM_INODES


# values from svfs.h
SV_MAGIC_NO = 134
SV_VERSION = 1

NUM_FREE_BLOCK_SLOTS = 30
NUM_DIRECT_BLOCK_SLOTS = 28


# root directory
ROOT_INODE = 1
ROOT_DATA_BLOCK = DATA_BLOCK_START_BLOCK

FIRST_FREE_INODE = 2


# location of the blank disk image
IMAGE_PATH = Path("sys/empty-fs.iso")


# encode values using the host's native C long representation
C_LONG = struct.Struct("@l")


def write_longs(image, block_number, values):

    # convert the disk block number into a byte offset
    base = block_number * BLOCK_SIZE

    # # check that the values fit inside this one block
    # required_bytes = len(values) * C_LONG.size
    # if required_bytes > BLOCK_SIZE:
    #     raise ValueError("Fields do not fit in one disk block")

    # pack each integer into the image, one after another
    for i, value in enumerate(values):
        C_LONG.pack_into(
            image,
            base + i * C_LONG.size,
            value,
        )


def load_image():

    image = bytearray(IMAGE_PATH.read_bytes())

    expected_size = NUM_BLOCKS * BLOCK_SIZE

    if len(image) != expected_size:
        raise ValueError(
            f"Expected {expected_size} bytes, "
            f"but found {len(image)} bytes"
        )

    return image


def write_superblock(
    image,
    *,
    free_inode,
    nfree,
    free_block_slots,
    root_inode,
    version,
):

    if len(free_block_slots) != NUM_FREE_BLOCK_SLOTS:
        raise ValueError("Expected exactly 30 free-block slots")

    values = [
        SV_MAGIC_NO,
        free_inode,
        nfree,
        *free_block_slots,
        root_inode,
        NUM_INODES,
        version,
    ]

    write_longs(image, SUPERBLOCK_ID, values)


def write_inode(
    image,
    inode_number,
    *,
    first_field,
    count,
    direct_block_slots,
    indirect_block,
):

    if not 1 <= inode_number <= NUM_INODES:
        raise ValueError(f"Invalid inode number: {inode_number}")

    if len(direct_block_slots) != NUM_DIRECT_BLOCK_SLOTS:
        raise ValueError("Expected exactly 28 direct-block slots")

    values = [
        first_field,
        inode_number,
        count,
        *direct_block_slots,
        indirect_block,
    ]

    write_longs(image, inode_number, values)


NAME_LEN = 24


def write_root_directory(image):
    block_offset = ROOT_DATA_BLOCK * BLOCK_SIZE

    # clear the entire root directory block first
    image[block_offset:block_offset + BLOCK_SIZE] = (
        b"\x00" * BLOCK_SIZE
    )

    # each dirent_t contains:
    #   long d_ino
    #   long d_off
    #   char d_name[NAME_LEN + 1]
    entry_size = 2 * C_LONG.size + NAME_LEN + 1

    entries = [
        (ROOT_INODE, entry_size, "."),
        (ROOT_INODE, 2 * entry_size, ".."),
    ]

    for index, (inode_number, next_offset, name) in enumerate(entries):
        entry_offset = block_offset + index * entry_size

        C_LONG.pack_into(image, entry_offset, inode_number)
        C_LONG.pack_into(
            image, entry_offset + C_LONG.size, next_offset
        )

        name_bytes = name.encode("ascii") + b"\x00"
        name_offset = entry_offset + 2 * C_LONG.size
        image[name_offset:name_offset + len(name_bytes)] = name_bytes


def initialize_root_inode(image):
    write_inode(
        image,
        ROOT_INODE,
        first_field=BLOCK_SIZE,  # directory size: one block
        count=1,                 # initial reference count
        direct_block_slots=[
            ROOT_DATA_BLOCK
        ] + [0] * (NUM_DIRECT_BLOCK_SLOTS - 1),
        indirect_block=0,
    )

    write_root_directory(image)



def initialize_free_inodes(image):

    for inode_number in range(FIRST_FREE_INODE, NUM_INODES + 1):

        if inode_number < NUM_INODES:
            next_free = inode_number + 1
        else:
            next_free = 0

        write_inode(
            image,
            inode_number,
            first_field=next_free,
            count=0,
            direct_block_slots=[0] * NUM_DIRECT_BLOCK_SLOTS,
            indirect_block=0,
        )


def initialize_data_block_free_list(image):

    # block 33 is reserved for the root directory
    free_blocks = list(
        range(ROOT_DATA_BLOCK + 1, NUM_BLOCKS)
    )

    # the first 29 entries go directly into the superblock
    ordinary_blocks = free_blocks[:29]

    # the 30th entry points to a block holding the remaining list
    overflow_block = free_blocks[29]
    overflow_contents = free_blocks[30:]

    # all 30 superblock entries: 29 ordinary entries + overflow pointer
    superblock_slots = ordinary_blocks + [overflow_block]

    # the overflow block's contents will be written in the next excerpt


    # proposed overflow format:
    # first long = number of free blocks listed
    # following longs = the free block numbers themselves
    overflow_values = [
        len(overflow_contents),
        *overflow_contents,
    ]

    write_longs(
        image,
        overflow_block,
        overflow_values,
    )

    return superblock_slots



def main():
    # load the blank filesystem image into memory
    image = load_image()

    # initialize the free-data-block list and overflow block
    free_block_slots = initialize_data_block_free_list(image)

    # initialize the free-inode chain (inodes 2 through 32)
    initialize_free_inodes(image)

    # initialize inode 1 and its root directory block
    initialize_root_inode(image)

    # write the superblock
    write_superblock(
        image,
        free_inode=FIRST_FREE_INODE,
        nfree=NUM_FREE_BLOCK_SLOTS,
        free_block_slots=free_block_slots,
        root_inode=ROOT_INODE,
        version=SV_VERSION,
    )

    # save the completed image
    IMAGE_PATH.write_bytes(image)

    print(f"Filesystem image initialized at {IMAGE_PATH}")

if __name__ == "__main__":
    main()



# from pathlib import Path
# import struct

# BLOCK_SIZE = 4096
# NUM_BLOCKS = 161
# IMAGE_PATH = Path("sys/empty-fs.iso")

# SV_MAGIC_NO = 134

# SV_SUPER_BLOCK = 0
# NUM_INODES = 32
# NUM_FREE_BLOCKS_SLOTS = 30
# INODE_START_BLOCK = 1

# NUM_DIRECT_BLOCKS_SLOTS = 28

# ROOT_INODE = 1
# SV_VERSION = 1

# DATA_BLOCK_START_INDEX = NUM_INODES

# C_LONG = struct.Struct("@l")


# def write_superblock(
#     image,
#     block_number,
#     *,
#     free_inode,
#     nfree,
#     free_block_slots,
#     root_inode,
#     version
# ):
#     if len(free_block_slots) != NUM_FREE_BLOCKS_SLOTS:
#         raise ValueError("Expected exactly 30 free-block slots")

#     values = [
#         SV_MAGIC_NO,
#         free_inode,
#         nfree,
#         *free_block_slots,
#         root_inode,
#         NUM_INODES,
#         version
#     ]

#     base = block_number * BLOCK_SIZE
#     for index, value in enumerate(values):
#         C_LONG.pack_into(
#             image,
#             base + index * C_LONG.size, 
#             value
#         )


# def write_inode(
#     image,
#     inode_number,
#     *,
#     un,
#     number,
#     count,
#     direct_block_slots,
#     indirect_block
# ):

#     if not 0 <= inode_number < NUM_INODES:
#         raise ValueError(f"Inode index {inode_number} is out of range")


#     if len(direct_block_slots) != NUM_DIRECT_BLOCKS_SLOTS:
#         raise ValueError("Expected exactly 28 direct-block slots")

#     values = [
#         un,
#         number,
#         count,
#         *direct_block_slots,
#         indirect_block
#     ]

    
#     base = inode_number * BLOCK_SIZE
    
#     for index, value in enumerate(values):
#         C_LONG.pack_into(
#             image, 
#             base + index * C_LONG.size, 
#             value
#         )


# def main():
#     image = bytearray(IMAGE_PATH.read_bytes())

#     expected_size = BLOCK_SIZE * NUM_BLOCKS

#     if len(image) != expected_size:
#         raise ValueError(
#             f"Expected {expected_size} bytes, but found {len(image)} bytes instead."
#         )
#     print(f"Loaded a {len(image)}-byte disk image")


#     # create blocks ids
#     block_ids = list(range(NUM_BLOCKS)) # index 0-160 (total 161 blocks)
#     superblock_id = block_ids[0] # index 0
#     inode_ids = block_ids[1:NUM_INODES+1] # index 1-32
#     data_block_ids = block_ids[NUM_INODES+1:] # index 33-160

#     free_data_blocks = data_block_ids # exclude any block already used by "/"
#     spill_block = free_data_blocks[29]
#     superblock_slots = free_data_blocks[:29] + [spill_block]
#     remaining_blocks = free_data_blocks[30:]

#     # store the remaining block IDs, in the spill block


#     superblock_data_blocks = data_block_ids[:NUM_FREE_BLOCKS_SLOTS]

#     # write super block
#     write_superblock(
#         image,
#         superblock_id,
#         free_inode=1, # inodes are 1-indexed, and first inode is used by "/"
#         nfree=NUM_FREE_BLOCKS_SLOTS,
#         free_block_slots=superblock_data_blocks,
#         root_inode=ROOT_INODE,
#         version=SV_VERSION,
#     )

#     # write inodes
#     for number in range(1, NUM_INODES + 1):
#         next_free = number + 1 if number < NUM_INODES else 0;

#         write_inode(
#             image,
#             number+1,
#             un=next_free,
#             number=inode_index,
#             count=0,
#             direct_block_slots=[0] * NUM_DIRECT_BLOCKS_SLOTS,
#             indirect_block=0,
#         )


#     IMAGE_PATH.write_bytes(image)
#     print(f"Initialized superblock and inodes in {IMAGE_PATH}")



# if __name__ == "__main__":
#     main()
