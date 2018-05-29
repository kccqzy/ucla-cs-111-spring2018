#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#pragma pack(1)

#include "ext2_fs.h"

#define DUMP_VAR_INT(x)                                                        \
  do { fprintf(stderr, #x " = %d\n", (int) (x)); } while (0)

static inline size_t
div_ceil(size_t a, size_t b) {
  if (!a) { return 0; }
  return ((a - 1) / b) + 1;
}

static void
analyze(const uint8_t* image, size_t size) {
  /* The superblock is always at byte offset 1024. */
  assert(size >= 1024 + sizeof(struct ext2_super_block));
  const struct ext2_super_block* s =
    (const struct ext2_super_block*) (image + 1024);
  size_t block_size = EXT2_MIN_BLOCK_SIZE << s->s_log_block_size;

  printf("SUPERBLOCK,%d,%d,%zu,%d,%d,%d,%d\n", s->s_blocks_count,
         s->s_inodes_count, block_size, s->s_inode_size, s->s_blocks_per_group,
         s->s_inodes_per_group, s->s_first_ino);

  size_t blocks_count = s->s_blocks_count;
  DUMP_VAR_INT(blocks_count);
  DUMP_VAR_INT(s->s_first_data_block);
  size_t groups_count =
    div_ceil(blocks_count - s->s_first_data_block, s->s_blocks_per_group);
  assert(groups_count == 1); /* Currently only one group is supported. */

  // The block group descriptor table starts on the first block following the
  // superblock. This would be the third block on a 1KiB block file system, or
  // the second block for 2KiB and larger block file systems.
  const struct ext2_group_desc* bgdt =
    (const struct ext2_group_desc*) (image + (s->s_log_block_size == 0
                                                ? 2 * block_size
                                                : block_size));

  size_t last_group_block_count =
    (blocks_count - s->s_first_data_block) % s->s_blocks_per_group;
  (void) last_group_block_count;
  /* This last_group_block_count is the correct block count for group 0. Stupid
     Mark Kampe disagreeing with ext2 developers on Piazza. */

  printf("GROUP,0,%zu,%d,%d,%d,%d,%d,%d\n",
         blocks_count,
         s->s_inodes_per_group, bgdt->bg_free_blocks_count,
         bgdt->bg_free_inodes_count, bgdt->bg_block_bitmap,
         bgdt->bg_inode_bitmap, bgdt->bg_inode_table);

  /* Now find all free blocks. */
  size_t block_bitmap_loc = bgdt->bg_block_bitmap;
  const uint8_t* block_bitmap = image + block_size * block_bitmap_loc;
  for (size_t blk = s->s_first_data_block, blk_end = blocks_count;
       blk < blk_end; ++blk) {
    /* Is this block free? */
    size_t i = blk - s->s_first_data_block; /* i is the relative index inside
                                               this group. */
    if (!(block_bitmap[i / 8] & (1 << (i % 8)))) { printf("BFREE,%zu\n", blk); }
  }

  /* Now find all free inodes. */
  size_t inode_bitmap_loc = bgdt->bg_inode_bitmap;
  const uint8_t* inode_bitmap = image + block_size * inode_bitmap_loc;
  for (size_t i = 0; i < s->s_inodes_per_group; ++i) {
    if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
      printf("IFREE,%zu\n", i + 1); /* inodes are numbered starting from 1 */
    }
  }

  /* Now scan all inodes. */
  size_t inode_table_loc = bgdt->bg_inode_table;
  const struct ext2_inode* inode_table =
    (const struct ext2_inode*) (image + block_size * inode_table_loc);
  DUMP_VAR_INT(inode_table_loc);
  for ( // size_t i = s->s_rev_level == 0 ? EXT2_GOOD_OLD_FIRST_INO :
        // s->s_first_ino;
    size_t i = 0; i < s->s_inodes_per_group; ++i) {
    if (inode_table[i].i_mode && inode_table[i].i_links_count) {
      uint16_t file_type = inode_table[i].i_mode & 0xf000;
      char mtime[20], ctime[20], atime[20];
      strftime(mtime, 20, "%D %T", gmtime(&(time_t){inode_table[i].i_mtime}));
      strftime(ctime, 20, "%D %T", gmtime(&(time_t){inode_table[i].i_ctime}));
      strftime(atime, 20, "%D %T", gmtime(&(time_t){inode_table[i].i_atime}));
      printf(
        "INODE,%zu,%c,%03o,%d,%d,%d,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%"
        "d,%d,%d,%d,%d,%d\n",
        i + 1,
        file_type == 0xa000
          ? 's'
          : file_type == 0x8000 ? 'f' : file_type == 0x4000 ? 'd' : '?',
        inode_table[i].i_mode &
          0xfff,              /* Low-order 12 bits are the actual mode */
        inode_table[i].i_uid, /* Owner */
        inode_table[i].i_gid, /* Group */
        inode_table[i].i_links_count, /* Links */
        ctime,                        /* Creation time */
        mtime,                        /* Modification time */
        atime,                        /* Access time */
        inode_table[i].i_size,        /* File size */
        inode_table[i].i_blocks,      /* Number of blocks reserved */
        inode_table[i].i_block[0],    /* fvck */
        inode_table[i].i_block[1],    /* fvck */
        inode_table[i].i_block[2],    /* fvck */
        inode_table[i].i_block[3],    /* fvck */
        inode_table[i].i_block[4],    /* fvck */
        inode_table[i].i_block[5],    /* fvck */
        inode_table[i].i_block[6],    /* fvck */
        inode_table[i].i_block[7],    /* fvck */
        inode_table[i].i_block[8],    /* fvck */
        inode_table[i].i_block[9],    /* fvck */
        inode_table[i].i_block[10],   /* fvck */
        inode_table[i].i_block[11],   /* fvck */
        inode_table[i].i_block[12],   /* fvck */
        inode_table[i].i_block[13],   /* fvck */
        inode_table[i].i_block[14]);
    }
  }
}

int
main(int argc, const char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s FILE\n", argv[0]);
    exit(1);
  }

  int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    fprintf(stderr, "%s: could not open '%s': %s\n", argv[0], argv[1],
            strerror(errno));
    exit(1);
  }

  struct stat st;
  int r = fstat(fd, &st);
  if (r == -1) {
    fprintf(stderr, "%s: could not stat '%s': %s\n", argv[0], argv[1],
            strerror(errno));
    exit(1);
  }

  if (!(st.st_mode & S_IFREG)) {
    fprintf(stderr, "%s: '%s' is not a regular file\n", argv[0], argv[1]);
    exit(1);
  }

  off_t sz = st.st_size;
  const uint8_t* image =
    (uint8_t*) mmap(NULL, sz, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
  if (image == MAP_FAILED) {
    fprintf(stderr, "%s: could not mmap: %s\n", argv[0], strerror(errno));
    exit(1);
  }

  analyze(image, sz);
}
