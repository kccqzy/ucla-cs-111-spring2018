#!/usr/bin/env python3

import csv
import sys
import collections

Superblock = collections.namedtuple('Superblock',
                                    ['total_blocks', 'total_inodes',
                                     'block_size', 'inode_size',
                                     'blocks_per_group', 'inodes_per_group',
                                     'first_non_reserved_inode'])

Group = collections.namedtuple('Group',
                               ['group_no', 'block_count', 'inode_count',
                                'free_block_count',
                                'free_inode_count', 'free_block_bitmap_loc',
                                'free_inode_bitmap_loc',
                                'first_inode_loc'])

INode = collections.namedtuple('INode',
                               ['inode_no', 'file_type', 'mode', 'owner',
                                'group', 'link_count', 'ctime',
                                'mtime', 'atime', 'size', 'blocks',
                                'block_addresses'])

DirEnt = collections.namedtuple('DirEnt',
                                ['parent_inode', 'logic_byte_offset', 'inode',
                                 'entry_length',
                                 'name_length', 'name'])

Indirect = collections.namedtuple('Indirect',
                                  ['inode', 'level', 'logical_block_offset',
                                   'indirect_block_loc', 'block_number'])

Filesystem = collections.namedtuple('Filesystem',
                                    ['super', 'group0', 'bfrees', 'ifrees',
                                     'inodes', 'dirents', 'indirects'])


def main():
    args = sys.argv
    if len(args) != 2:
        print("usage: {} [FILE]".format(args[0] if len(args) >= 1 else "PROG"),
              file=sys.stderr)
        exit(1)

    fs = parse(args[1])
    block_consistency_audit(fs)
    allocated_inodes = inode_allocation_audit(fs)
    directory_consistency_audit(fs, allocated_inodes)


def parse(path):
    with open(path, newline='') as f:
        reader = csv.reader(f)
        bfrees = set()
        ifrees = set()
        inodes = []
        dirents = []
        indirects = []
        for row in reader:
            if row[0] == 'SUPERBLOCK':
                super = Superblock(*(map(int, row[1:])))
            elif row[0] == 'GROUP':
                group0 = Group(*(map(int, row[1:])))
            elif row[0] == 'BFREE':
                bfrees.add((int(row[1])))
            elif row[0] == 'IFREE':
                ifrees.add((int(row[1])))
            elif row[0] == 'INODE':
                inodes.append(
                    INode(inode_no=int(row[1]),
                          file_type=row[2],
                          mode=row[3],
                          owner=int(row[4]),
                          group=int(row[5]),
                          link_count=int(row[6]),
                          ctime=row[7],
                          mtime=row[8],
                          atime=row[9],
                          size=int(row[10]),
                          blocks=int(row[11]),
                          block_addresses=list(map(int, row[12:]))
                          ))
            elif row[0] == 'DIRENT':
                dirents.append(DirEnt(*(map(int, row[1:6])), name=row[6]))
            elif row[0] == 'INDIRECT':
                indirects.append(Indirect(*(map(int, row[1:]))))
    return Filesystem(bfrees=bfrees, ifrees=ifrees, inodes=inodes,
                      dirents=dirents, indirects=indirects,
                      super=super, group0=group0)


def block_consistency_audit(fs):
    total_blocks = fs.super.total_blocks
    block_descriptions = {}
    block_descriptions[fs.group0.free_block_bitmap_loc] = [
        "FREE BLOCK BITMAP FOR GROUP 0"]
    block_descriptions[fs.group0.free_inode_bitmap_loc] = [
        "FREE INODE BITMAP FOR GROUP 0"]
    block_descriptions[fs.group0.first_inode_loc] = ["INODE TABLE FOR GROUP 0"]
    first_data_block = total_blocks + 1

    for inode in fs.inodes:
        def check(i, b, what=''):
            if b == 0:
                return True
            description = f'{what:s}BLOCK {b:d} IN INODE {inode.inode_no:d} AT OFFSET {i:d}'
            if b < 0 or b >= total_blocks:
                print('INVALID', description)
                return False
            if 0 < b <= 3:
                print('RESERVED', description)
                return False
            if b in block_descriptions:
                # Duplicate found.
                if len(block_descriptions[b]) == 1:
                    # first time found a dup
                    print('DUPLICATE', block_descriptions[b][0])
                print('DUPLICATE', description)
                block_descriptions[b].append(description)
            else:
                block_descriptions[b] = [description]
            nonlocal first_data_block
            first_data_block = min(b, first_data_block)
            return True

        if inode.file_type == 'f' or inode.file_type == 'd':

            # Look at the 12 direct blocks.
            for i, b in enumerate(inode.block_addresses[:12]):
                check(i, b)

            def get_indirect_with(level, loc):
                yield from filter(
                    lambda
                        k: k.inode == inode.inode_no and k.level == level and k.indirect_block_loc == loc,
                    fs.indirects)

            offset = 12
            indirect, indirect2, indirect3 = inode.block_addresses[12:]
            if check(offset, indirect, what='INDIRECT '):
                # traverse the indirect block
                for b in get_indirect_with(level=1, loc=indirect):
                    check(b.logical_block_offset, b.block_number)
            offset = 268
            if check(offset, indirect2, what='DOUBLE INDIRECT '):
                for b in get_indirect_with(level=2, loc=indirect2):
                    if check(b.logical_block_offset, b.block_number):
                        for bb in get_indirect_with(level=1,
                                                    loc=b.block_number):
                            check(bb.logical_block_offset, bb.block_number)
            offset = 65804
            if check(offset, indirect3, what='TRIPLE INDIRECT '):
                for b in get_indirect_with(level=3, loc=indirect3):
                    if check(b.logical_block_offset, b.block_number):
                        for bb in get_indirect_with(level=2,
                                                    loc=b.block_number):
                            if check(bb.logical_block_offset, bb.block_number):
                                for bbb in get_indirect_with(level=1,
                                                             loc=bb.block_number):
                                    check(bbb.logical_block_offset,
                                          bbb.block_number)

    # Now check for unallocated blocks
    for b in range(first_data_block, total_blocks):
        # TODO XXX If the first data block is unallocated (say previously
        # belonging to a file since deleted), this heuristic might be wrong.
        if not ((b in block_descriptions) or (b in fs.bfrees)):
            print(f'UNREFERENCED BLOCK {b:d}')

    # Check for allocated blocks on the free list
    for b in set(block_descriptions.keys()).intersection(fs.bfrees):
        print(f'ALLOCATED BLOCK {b:d} ON FREELIST')


def inode_allocation_audit(fs):
    allocated_inodes = set(map(lambda k: k.inode_no, fs.inodes))
    free_inodes = fs.ifrees
    for allocated_on_free in allocated_inodes.intersection(free_inodes):
        print(f'ALLOCATED INODE {allocated_on_free:d} ON FREELIST')
    all_inodes = set(
        range(fs.super.first_non_reserved_inode, fs.super.total_inodes + 1))
    all_inodes.add(2)
    # special handling for the root directory; stupid
    for unallocated_not_on_free in all_inodes - allocated_inodes - free_inodes:
        print(f'UNALLOCATED INODE {unallocated_not_on_free:d} NOT ON FREELIST')
    return allocated_inodes


def directory_consistency_audit(fs, allocated_inodes):
    min_inode = 1
    max_inode = fs.super.total_inodes
    for dirent in fs.dirents:
        if dirent.inode < min_inode or dirent.inode > max_inode:
            print(
                f'DIRECTORY INODE {dirent.parent_inode:d} NAME {dirent.name} INVALID INODE {dirent.inode:d}')
        elif dirent.inode not in allocated_inodes:
            print(
                f'DIRECTORY INODE {dirent.parent_inode:d} NAME {dirent.name} UNALLOCATED INODE {dirent.inode:d}')

    # recompute link count
    actual_link_count = {}
    for dirent in fs.dirents:
        if dirent.inode in actual_link_count:
            actual_link_count[dirent.inode] += 1
        else:
            actual_link_count[dirent.inode] = 1
    for inode in fs.inodes:
        if inode.inode_no not in actual_link_count:
            print(
                f'INODE {inode.inode_no:d} HAS 0 LINKS BUT LINKCOUNT IS {inode.link_count:d}')
        elif inode.link_count != actual_link_count[inode.inode_no]:
            print(
                f'INODE {inode.inode_no:d} HAS {actual_link_count[inode.inode_no]:d} LINKS BUT LINKCOUNT IS {inode.link_count:d}')

    # Compute correctness of . and ..
    dir_parent = {2: 2}
    for dirent in fs.dirents:
        if dirent.name != "'.'" and dirent.name != "'..'":
            dir_parent[dirent.inode] = dirent.parent_inode

    for dirent in fs.dirents:
        if dirent.name == "'.'" and dirent.parent_inode != dirent.inode:
            print(
                f"DIRECTORY INODE {dirent.parent_inode:d} NAME '.' LINK TO INODE {dirent.inode:d} SHOULD BE {dirent.parent_inode:d}")
        elif dirent.name == "'..'" and dirent.inode != dir_parent[dirent.inode]:
            print(
                f"DIRECTORY INODE {dirent.parent_inode:d} NAME '..' LINK TO INODE {dirent.inode:d} SHOULD BE {dir_parent[dirent.parent_inode]:d}")


if __name__ == '__main__':
    main()
