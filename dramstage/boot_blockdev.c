/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <rk3399/payload.h>
#include <inttypes.h>
#include <assert.h>

#include <log.h>
#include <async.h>
#include <iost.h>
#include <dump_mem.h>
#include <byteorder.h>

static _Alignas(4096) u8 partition_table_buffer[4 * 4096];

enum iost boot_blockdev(struct async_blockdev *blk) {
	assert(blk->block_size <= 8192 && blk->block_size >= 128);
	assert(blk->block_size % 128 == 0);
	enum iost res;
	if (IOST_OK != (res = blk->start(blk, 0, partition_table_buffer, partition_table_buffer + sizeof(partition_table_buffer)))) {return res;}
	struct async_buf buf = blk->async.pump(&blk->async, 0, blk->block_size);
	if (buf.end < buf.start) {return buf.start - buf.end;}
	dump_mem(partition_table_buffer, 512);
	if (buf.start[510] != 0x55 || buf.start[511] != 0xaa) {
		puts("no MBR found\n");
		return IOST_INVALID;
	}
	for_range(partition, 0, 4) {
		if (buf.start[446 + 16 * partition + 4] == 0xee) {
			goto gpt_found;
		}
	}
	puts("MBR does not contain a GPT entry\n");
	return IOST_INVALID;
	gpt_found:
	buf = blk->async.pump(&blk->async, 512, 2 * blk->block_size - 512);
	if (buf.end < buf.start) {return buf.start - buf.end;}
	buf.start += blk->block_size - 512;
	dump_mem(buf.start, 92);
	if (from_le64(*(u64 *)buf.start) != 0x5452415020494645) {
		puts("wrong GPT signature\n");
		return IOST_INVALID;
	}
	if (from_le32(*(u32 *)(buf.start + 8)) != 0x00010000) {
		puts("unrecognized GPT version\n");
		return IOST_INVALID;
	}
	if (from_le32(*(u32 *)(buf.start + 12)) != 92) {
		puts("unexpected GPT header size\n");
		return IOST_INVALID;
	}
	if (from_le64(*(u64 *)(buf.start + 24)) != 1) {
		puts("current LBA does not match\n");
		return IOST_INVALID;
	}
	u64 backup_lba = from_le64(*(u64 *)(buf.start + 32));
	if (backup_lba != blk->num_blocks - 1) {
		puts("backup header not on last LBA\n");
		return IOST_INVALID;
	}
	u64 first_usable_lba = from_le64(*(u64 *)(buf.start + 40));
	u64 last_usable_lba = from_le64(*(u64 *)(buf.start + 48));
	u32 num_partition_entries = from_le32(*(u32 *)(buf.start + 80));
	u32 entries_per_block = blk->block_size / 128;
	u32 partition_table_size = num_partition_entries / entries_per_block;
	if (first_usable_lba < 2 + partition_table_size) {
		puts("not enough space reserved for primary partition table entries\n");
		return IOST_INVALID;
	}
	if (last_usable_lba > blk->num_blocks - 2 - partition_table_size) {
		puts("not enough space reserved for secondary partition table entries\n");
		return IOST_INVALID;
	}
	if (num_partition_entries > 0x10000) {
		printf("refusing to read %"PRIu32" partition table entries\n", num_partition_entries);
		return IOST_INVALID;
	}
	buf = blk->async.pump(&blk->async, blk->block_size, 128);
	if (buf.end < buf.start) {return buf.start - buf.end;}
	u32 mask = 0;
	u64 first[3], last[3];
	u8 str[2] = "A";
	for_range(i, 0, num_partition_entries) {
		if (buf.end < buf.start) {return buf.start - buf.end;}
#ifdef DEBUG_MSG
		dump_mem(buf.start, 128);
#endif
		if ((size_t)(buf.end - buf.start) < 128) {
			blk->start(blk,
				2 + i / entries_per_block,
				partition_table_buffer,
				partition_table_buffer + sizeof(partition_table_buffer)
			);
			buf = blk->async.pump(&blk->async, 0, 128);
		}
		for_range(j, 0, 16) {
			if (buf.start[j] != 0) {goto entry_exists;}
		}
		debug("entry %"PRIu32": empty\n", i);
		buf = blk->async.pump(&blk->async, 128, i < num_partition_entries - 1 ? 128 : 0);
		continue;
	entry_exists:;
		u64 head = from_le64(*(u64 *)buf.start);
		u64 tail = from_be64(*(u64 *)(buf.start + 8));
		printf("entry %"PRIu32": %08"PRIx32"-%04"PRIx16"-%04"PRIx64"-%04"PRIx64"-%012"PRIx64,
			i, (u32)head, (u16)(head >> 32), head >> 48, tail >> 48, tail & 0xffffffffffff
		);
		u32 index = 3;
		if (head == 0x46f68e5ee5ab07a0 && tail == 0x9ce841a518929b7c) {index = 0;}
		if (head == 0x4b6dc9205f04b556 && tail == 0xbd77804efe6fae01) {index = 1;}
		if (head == 0x4b78d766c195cc59 && tail == 0x813fa0e1519099d8) {index = 2;}
		if (index >= 3) {
			puts(" ignored\n");
		} else if (mask & 1 << index) {
			puts(" duplicate ignored\n");
		} else {
			first[index] = from_le64(*(u64 *)(buf.start + 32));
			last[index] = from_le64(*(u64 *)(buf.start + 40));
			str[0] = 'A' + index;
			printf(" levinboot payload %s: %"PRIu64"–%"PRIu64"\n", str, first[index], last[index]);
			if (first[index] > last[index] || last[index] > last_usable_lba) {
				puts("bogus LBAs, ignoring");
			} else {
				mask |= 1 << index;
			}
		}
		buf = blk->async.pump(&blk->async, 128, i < num_partition_entries - 1 ? 128 : 0);
	}
	if (mask == 0) {
		infos("no payload partitions\n");
		return IOST_INVALID;
	}
	u32 used_index = 0;
	if (mask == 2 || mask == 6) {used_index = 1;}
	if (mask == 4 || mask == 5) {used_index = 2;}
	u64 used_first = first[used_index], used_last = last[used_index];
	str[0] = 'A' + used_index;
	printf("using payload %s\n", str);
	u32 max_length = ((60 << 20) + blk->block_size - 1) / blk->block_size;
	if (used_last - used_first >= max_length) {
		puts("selected payload partition is larger than the buffer, clipping\n");
		used_last = used_first + max_length - 1;
	}
	if (IOST_OK != (res = blk->start(blk, used_first, blob_buffer.start, blob_buffer.end))) {return res;}
	if (IOST_OK != (res = decompress_payload(&blk->async))) {return IOST_INVALID;}
	return IOST_OK;
}
