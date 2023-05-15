#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h> //added this
#include <math.h>

#include "disk.h"
#include "fs.h"

#define fsSignature "ECS150FS"
#define FAT_EOC 0xFFFF

typedef enum
{
	NOT_MOUNTED,
	MOUNTED,
} Mount;

/* TODO: Phase 1 */

struct superblock
{
	uint8_t signature[8];
	uint16_t totalBlocks;
	uint16_t rdirIndex;
	uint16_t dataBlockIndex;
	uint16_t totalDataBlocks;
	uint8_t totalFatBlocks;
	uint8_t padding[4079];
};

struct rEntry
{
	uint8_t filename[FS_FILENAME_LEN];
	uint32_t fileSize;
	uint16_t index;
	int8_t padding[10];
};

struct rootDirectory
{
	struct rEntry entries[128];
};

typedef struct
{
	uint8_t filename[FS_FILENAME_LEN];
	size_t offset;
	int index;
} file_descriptor;

static struct superblock sb;
static uint16_t *FATblock;
static struct rootDirectory rd;
file_descriptor fdTable[32];

Mount isMounted = NOT_MOUNTED;

int fs_mount(const char *diskname)
{
	/* TODO: Phase 1 */
	if (diskname == NULL)
		return -1;
	int openDisk = block_disk_open(diskname);
	if (openDisk == -1)
		return -1;
	// read superblock
	block_read(0, &sb);
	// **error management**
	// check signature
	for (int i = 0; i < 8; i++)
	{
		if (fsSignature[i] != sb.signature[i])
		{
			return -1;
		}
	}
	// check FAT count
	if (sb.totalFatBlocks != (int)ceil((sb.totalDataBlocks * 2.0) / 4096))
	{
		return -1;
	}
	// check index
	if (sb.rdirIndex != sb.totalFatBlocks + 1)
	{
		return -1;
	}
	if (sb.dataBlockIndex != sb.rdirIndex + 1)
	{
		return -1;
	}
	// check disk count
	if (block_disk_count() != sb.totalBlocks)
	{
		block_disk_close();
		return -1;
	}

	// read FATblock
	FATblock = malloc(((sb.totalFatBlocks * 2048) * sizeof(uint16_t)));
	uint16_t *FAToffset = FATblock;
	for (int i = 1; i < sb.rdirIndex; i++)
	{
		block_read(i, FAToffset);
		FAToffset += 2048;
	}
	// read rootDir
	block_read(sb.rdirIndex, &rd);
	isMounted = MOUNTED;
	return 0;
}

int fs_umount(void)
{
	/* TODO: Phase 1 */
	// if not mounted
	if (!isMounted)
	{
		return -1;
	}

	// Write the superblock content
	block_write(0, &sb);

	for (int i = 0; i < sb.totalFatBlocks; i++)
	{
		block_write(i + 1, FATblock + (i * BLOCK_SIZE));
	}

	block_write(sb.rdirIndex, &rd);

	block_disk_close();
	isMounted = NOT_MOUNTED;
	return 0;
}

int fs_info(void)
{
	/* TODO: Phase 1 */
	if (!isMounted)
	{
		return -1;
	}
	printf("FS Info:\n");
	printf("total_blk_count=%d\n", sb.totalBlocks);
	printf("fat_blk_count=%d\n", sb.totalFatBlocks);
	printf("rdir_blk=%d\n", sb.rdirIndex);
	printf("data_blk=%d\n", sb.dataBlockIndex);
	printf("data_blk_count=%d\n", sb.totalDataBlocks);
	// count remaining data blocks
	int FATblockRemaining = sb.totalDataBlocks - 1;
	for (int i = 1; i < (sb.totalFatBlocks * 2048); i++)
	{
		if (FATblock[i] != 0)
		{
			FATblockRemaining--;
		}
	}
	printf("fat_free_ratio=%d/%d\n", FATblockRemaining, sb.totalDataBlocks);
	// count remaining file entries
	int fileRemaining = 128;
	for (int i = 0; i < 128; i++)
	{
		if (rd.entries[i].filename[0] != '\0')
		{
			fileRemaining--;
		}
	}
	printf("rdir_free_ratio=%d/%d\n", fileRemaining, 128);
	return 0;
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
	// Check the name
	if (strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0' || filename == NULL)
	{
		return -1;
	}

	// Check if disk is mounted
	if (!isMounted)
	{
		return -1;
	}

	// Check if the drive is at capacity
	int empty_index = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(filename, (const char *)rd.entries[i].filename) == 0)
		{
			return -1;
		}

		if (rd.entries[i].filename[0] == '\0')
		{
			if (empty_index == -1)
			{
				empty_index = i;
			}
		}
	}

	// Check if there are any spot available for the file
	if (empty_index == -1)
	{
		return -1;
	}

	memcpy((char *)rd.entries[empty_index].filename, filename, strlen(filename));
	rd.entries[empty_index].fileSize = 0;
	rd.entries[empty_index].index = FAT_EOC;

	return 0;
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
	// Check the file name
	if (strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0' || filename == NULL)
		return -1;

	// Check if mounted
	if (!isMounted)
		return -1;

	int delete_index = -1;

	// Find the index of file to delete
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(filename, (const char *)rd.entries[i].filename) == 0)
		{
			delete_index = i;
			break;
		}
	}

	// if file is not found
	if (delete_index == -1)
		return -1;

	memset(rd.entries[delete_index].filename, '\0', FS_FILENAME_LEN);
	rd.entries[delete_index].fileSize = 0;

	uint16_t fat_index = rd.entries[delete_index].index;
	char *buffer = malloc(sizeof(uint8_t) * BLOCK_SIZE);

	while (fat_index != FAT_EOC)
	{
		block_read(fat_index + sb.dataBlockIndex, buffer);
		memset(buffer, '\0', BLOCK_SIZE);
		block_write(fat_index + sb.dataBlockIndex, buffer);
		uint16_t next = FATblock[fat_index];
		FATblock[fat_index] = 0;
		fat_index = next;
	}

	return 0;
}

int fs_ls(void)
{
	if (!isMounted)
		return -1;

	/* TODO: Phase 2 */
	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (rd.entries[i].filename[0] != '\0')
		{
			printf("file: %s, ", rd.entries[i].filename);
			printf("size: %u, ", rd.entries[i].fileSize);
			printf("data_blk: %i\n", rd.entries[i].index);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
	if (strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0' || filename == NULL)
		return -1;

	// Check if mounted
	if (!isMounted)
		return -1;

	int file_index = -1;

	// check if file exists
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(filename, (const char *)rd.entries[i].filename) == 0)
		{
			file_index = i;
			break;
		}
	}

	if (file_index == -1)
		return -1;

	int fd_index = -1;

	for (int i = 0; i < (int)sizeof(fdTable); i++)
	{

		if (fdTable[i].filename[0] == '\0')
		{
			fd_index = i;
			break;
		}
	}

	if (fd_index == -1)
		return -1;

	fdTable[fd_index].offset = 0;
	strcpy((char *)fdTable[fd_index].filename, filename);
	fdTable[fd_index].index = file_index;
	return fd_index;
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */

	// Check if mounted
	if (!isMounted)
		return -1;
	// Check if fd is within bounds
	if (fd > 31 || fd < 0)
		return -1;
	fdTable[fd].filename[0] = '\0';
	return 0;
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
	// Check if mounted
	if (!isMounted)
		return -1;
	// Check if fd is within bounds
	if (fd > 31 || fd < 0)
		return -1;
	return rd.entries[fdTable[fd].index].fileSize;
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
	// Check if mounted
	if (!isMounted)
		return -1;
	// Check if fd is within bounds
	if (fd > 31 || fd < 0)
		return -1;
	if (offset > (size_t)rd.entries[fdTable[fd].index].fileSize)
		return -1;
	fdTable[fd].offset = offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
	// Check if buffer null
	if (buf == NULL)
		return -1;

	if (count == 0)
		return -1;

	// Check if disk is mounted
	if (!isMounted)
		return -1;

	// Check bounds
	if (fd > 31 || fd < 0)
		return -1;

	// Check if fd is open
	if (fdTable[fd].filename[0] == '\0')
		return -1;

	// find the file in root dir
	int rd_file_index = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp((const char *)rd.entries[i].filename, (const char *)fdTable[fd].filename) == 0)
		{
			rd_file_index = i;
			break;
		}
	}

	if (rd_file_index == -1)
		return -1;

	size_t bytes_written = 0;
	size_t bytes_remaining = count;
	int32_t offset = fdTable[fd].offset;
	uint16_t current_db = rd.entries[rd_file_index].index;
		
	// find empty data block based on offset
	int numDataBlocks = offset / 4096;
	for (int i = 0; i < numDataBlocks; i++)
	{
		if (FATblock[current_db] != FAT_EOC)
		{
			current_db = FATblock[current_db];
		}
		else
		{
			break;
		}
	}

	while (bytes_remaining > 0)
	{
		if (current_db == FAT_EOC)
		{
			int free_db_index = -1;
			for (int i = 0; i < sb.totalDataBlocks; i++)
			{
				if (FATblock[i] == 0)
				{
					free_db_index = i;
					break;
				}
			}

			if (free_db_index == -1)
				return bytes_written;

			FATblock[free_db_index] = FAT_EOC;
			rd.entries[rd_file_index].index = free_db_index;
			current_db = free_db_index;
		}

		size_t block_pos = offset % BLOCK_SIZE;

		// Check how much bytes remain in a block
		// And if bytes remaining fit in the block
		size_t bytes_to_write = BLOCK_SIZE - block_pos;
		if (bytes_to_write > bytes_remaining)
			bytes_to_write = bytes_remaining;

		char *bounce = malloc(sizeof(uint8_t) * BLOCK_SIZE);

		// Read whats in the file
		block_read(sb.dataBlockIndex + current_db, bounce);

		// Add the bytes to be written
		memcpy(bounce + block_pos, buf + bytes_written, bytes_to_write);

		// write back to file
		block_write(sb.dataBlockIndex + current_db, bounce);

		// update file size if the data cannot fit
		if (offset + bytes_to_write > rd.entries[rd_file_index].fileSize)
			rd.entries[rd_file_index].fileSize = offset + bytes_to_write;

		// Update trackers
		bytes_written += bytes_to_write;
		bytes_remaining -= bytes_to_write;
		offset += bytes_to_write;
		fdTable[fd].offset = offset;

		// Check if there are still bytes
		// remaining to be written
		// and find an empty data block
		if (bytes_remaining > 0)
		{
			int free_db_index = -1;
			for (int i = 0; i < sb.totalDataBlocks; i++)
			{
				if (FATblock[i] == 0)
				{
					free_db_index = i;
					break;
				}
			}

			if (free_db_index == -1)
				return bytes_written;

			FATblock[current_db] = free_db_index;
			FATblock[free_db_index] = FAT_EOC;
			current_db = FATblock[current_db];
		}
		free(bounce);
	}

	block_write(sb.totalFatBlocks, FATblock);
	block_write(sb.rdirIndex, &rd);

	return bytes_written;
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
	// Check if mounted
	if (!isMounted)
		return -1;
	// Check if fd is within bounds
	if (fd > 31 || fd < 0)
		return -1;
	if (buf == NULL)
		return -1;

	// start at index of first block and shift to where offset is
	int offsetStart = fdTable[fd].offset;
	int currOffset = fdTable[fd].offset;
	int dataBlkStart = rd.entries[fdTable[fd].index].index;
	int dataBlkBound = 4096;
	int numDataBlocks = currOffset / 4096;
	for (int i = 0; i < numDataBlocks; i++)
	{
		if (FATblock[dataBlkStart] != FAT_EOC)
		{
			dataBlkStart = FATblock[dataBlkStart];
			dataBlkBound += 4096;
		}
		else
		{
			break;
		}
	}

	// count data blocks to access
	int readTo = currOffset + count;
	numDataBlocks = ceil((readTo) / 4096.0);
	if ((uint32_t)readTo > rd.entries[fdTable[fd].index].fileSize)
		readTo = rd.entries[fdTable[fd].index].fileSize;
	numDataBlocks = ceil((readTo) / 4096.0);
	char *bounce = malloc(4096);
	for (int i = 0; i < numDataBlocks; i++)
	{
		block_read(sb.rdirIndex + 1 + dataBlkStart, bounce);
		if (FATblock[dataBlkStart] != FAT_EOC)
		{
			dataBlkStart = FATblock[dataBlkStart];
		}
		// if perfect fit
		if (dataBlkBound - currOffset == 4096 && readTo >= dataBlkBound)
		{
			memcpy(buf, bounce, 4096);
			buf += 4096;
			currOffset += 4096;
		}
		else
		{
			int partialRead;
			if (dataBlkBound <= readTo)
			{
				partialRead = dataBlkBound - currOffset;
			}
			else
			{
				partialRead = readTo - currOffset;
			}
			bounce += (currOffset) % 4096;
			memcpy(buf, bounce, partialRead);
			buf += partialRead;
			currOffset += partialRead;
		}
		dataBlkBound += 4096;
	}
	fdTable[fd].offset = currOffset;
	return currOffset - offsetStart;
}
