#include "sfs_api.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk_emu.h"

#define MAX_FILE_NAME_LENGTH 16
#define MAX_FILE_EXTENSION_LENGTH 3
#define BLOCK_SIZE 1024
#define FILE_SYSTEM_SIZE 1024
#define NUM_OF_I_NODES 200
#define NUM_OF_FILES (NUM_OF_I_NODES - 1)

#pragma region Data Structures.
typedef struct super_block {
  int magic_number;         // This is a magic number.
  int block_size;           // The size of each block, in bytes.
  int file_system_size;     // The total number of blocks the disk has.
  int i_node_table_length;  // The number of blocks to contain all i-nodes.
  int i_node_num;           // The number of i-nodes we have in this disk.
  int root_directory;       // The pointer to the i-node for the root directory.
} super_block;

typedef struct i_node {
  int mode;  // The mode to operate on this file.
  int link_count;
  int uid;
  int gid;
  int size;                 // The size of the file.
  int direct_pointers[12];  // 12 direct pointers, each pointing to a data
                            // block.
  int indirect_pointer;     // One indirect pointer, which points to a block
                            // containing references to subsequent blocks.
} i_node;

typedef struct directory_entry {
  int i_node_id;  // The i-Node this directory points to.
  char file_name[MAX_FILE_NAME_LENGTH + MAX_FILE_EXTENSION_LENGTH + 1];
} directory_entry;

typedef struct fdt_entry {
  int i_node_idx;          // The idx of the i-Node this file points to.
  int read_write_pointer;  // The read/write pointer of this file.
} fdt_entry;
#pragma endregion

int I_NODE_TABLE_START = 1;
int I_NODE_TABLE_LENGTH;
int ROOT_DIRECTORY_START;
int ROOT_DIRECTORY_LENGTH;
int DATA_BLOCK_START;
int DATA_BLOCK_LENGTH;
int BITMAP_START;
int BITMAP_LENGTH;

// Cached variables.
i_node i_node_table[NUM_OF_I_NODES];
fdt_entry fdt_table[NUM_OF_FILES];
directory_entry root_directory_table[NUM_OF_FILES];
char bitmap[FILE_SYSTEM_SIZE / 8];

int files_visited = 0;

#pragma region General Utils
/**
 * @brief
 * Calculate the number of blocks necessary
 * for the specified file size.
 *
 * @param file_size The file size.
 * @return unsigned int The number of blocks.
 */

int calculate_block_length(unsigned int file_size) {
  // Note that the result depends on the block size.
  return file_size / BLOCK_SIZE + (file_size % BLOCK_SIZE) > 0;
}

/**
 * @brief
 * Write the specified data to a buffer.
 *
 * @param data The data to be written.
 * @param data_size The size of data, in bytes.
 * @return void* The pointer to the result buffer.
 */
void *write_data_to_a_buffer(void *data, unsigned int data_size) {
  unsigned int num_of_blocks_needed = calculate_block_length(data_size);
  unsigned int total_mem_size = num_of_blocks_needed * BLOCK_SIZE;
  void *buffer = (void *)malloc(total_mem_size);
  memset(buffer, 0, total_mem_size);
  memcpy(buffer, data, data_size);
  return buffer;
}
#pragma endregion

#pragma region Bitmap Utils
/**
 * @brief
 * Check whether the specified block is
 * available according to the bitmap.
 * @param block_id The ID of the block to investigate.
 * @return True If free.
 * @return False If occupied.
 */
bool bitmap_is_block_free(int block_id) {
  int row_num = block_id / 8, column_num = block_id % 8;
  return (bitmap[row_num] & (1 << column_num)) > 0;
}

/**
 * @brief
 * Free a block in the bitmap
 * by setting it to 1.
 * @param block_id The ID of the block to free.
 */
void bitmap_free_a_block(int block_id) {
  int row_num = block_id / 8, column_num = block_id % 8;
  bitmap[row_num] |= (1 << column_num);
}

/**
 * @brief
 * Occupy a block in the bitmap
 * by setting it to 0.
 * @param block_id The ID of the block to occupy.
 */
void bitmap_occupy_a_block(int block_id) {
  int row_num = block_id / 8, column_num = block_id % 8;
  bitmap[row_num] &= ~(1 << column_num);
}

/**
 * @brief
 * Find the first available block
 * according to the bitmap.
 * @return The ID of the first free block.
 */
int bitmap_find_first_available_block() {
  for (int i = 0; i < FILE_SYSTEM_SIZE; i++)
    if (bitmap_is_block_free(i)) return i;
  return -1;
}
#pragma endregion

#pragma region Flushing Utils
/**
 * @brief
 * Flush the cached i-Node table to the disk.
 */
void flush_i_node_table() {
  void *buffer = write_data_to_a_buffer(i_node_table, NUM_OF_I_NODES * sizeof(i_node));
  write_blocks(I_NODE_TABLE_START, I_NODE_TABLE_LENGTH, buffer);
  free(buffer);
}

/**
 * @brief
 * Flush the root directory table to the disk.
 */
void flush_root_directory_table() {
  void *buffer = write_data_to_a_buffer(root_directory_table, NUM_OF_FILES * sizeof(directory_entry));
  write_blocks(ROOT_DIRECTORY_START, ROOT_DIRECTORY_LENGTH, buffer);
}

/**
 * @brief
 * Flush the cached bitmap to the disk.
 */
void flush_bitmap() {
  void *buffer = write_data_to_a_buffer(bitmap, FILE_SYSTEM_SIZE / 8);
  write_blocks(BITMAP_START, BITMAP_LENGTH, buffer);
  free(buffer);
}
#pragma endregion

#pragma region Root Dir Utils
/**
 * @brief
 * Get the directory entry by the filename.
 * @param filename The filename.
 * @returns A pointer to the directory entry, if found.
 * @returns NULL, if not.
 */
directory_entry *root_get_directory_entry(const char *filename) {
  for (int i = 0; i < NUM_OF_FILES; i++)
    if (root_directory_table[i].i_node_id != -1 && strcmp(root_directory_table[i].file_name, filename) == 0)
      return &root_directory_table[i];
  return NULL;
}

/**
 * @brief
 * Get the number of files in the root.
 * @return The number of files.
 */
int root_count_number_of_files() {
  int result = 0;
  for (int i = 0; i < NUM_OF_FILES; i++)
    if (root_directory_table[i].i_node_id != -1) result++;
  return result;
}

/**
 * @brief
 * Get the directory entry
 * of the ith file in the root.
 * @param i Index.
 * @return The ith directory entry.
 */
directory_entry *root_get_ith_file(int i) {
  int count = 0;
  for (int j = 0; j < NUM_OF_FILES; ++j)
    if (root_directory_table[j].i_node_id != -1 && count++ == i) return &root_directory_table[i];
  return NULL;
}

/**
 * @brief
 * Find the first available
 * directory entry in the
 * root directory table.
 * @return The index of the available entry.
 */
int root_get_first_available_directory_entry() {
  for (int i = 0; i < NUM_OF_FILES; i++)
    if (root_directory_table[i].i_node_id == -1) return i;
  return -1;
}
#pragma endregion

#pragma region iNode Utils
/**
 * @brief
 * Get the first vacant i-Node.
 * @return The ID to the first available iNode.
 */
int inode_tab_get_first_available_entry() {
  for (int i = 0; i < NUM_OF_I_NODES; ++i)
    if (i_node_table[i].size != -1) return i;
  return -1;
}

/**
 * @brief
 * Get the block ID of the specified page offset.
 * @param node The iNode to investigate.
 * @param loc The page offset, the offset starts from 0.
 * @returns The block ID which should contain the offset.
 * @returns -1, if cannot find the block ID.
 */
int inode_get_block_id_by_offset(i_node *node, int loc) {
  int file_size = node->size;
  int indirect_ptr = node->indirect_pointer;
  int indirect_block[BLOCK_SIZE];

  if (file_size <= loc)
    return -1;
  else if (loc < 12 * BLOCK_SIZE)
    // The block should be in the direct pointers.
    return loc % BLOCK_SIZE;
  else {
    // The block should be in the indirect pointers.
    read_blocks(indirect_ptr, 1, indirect_block);
    return indirect_block[(loc - 12 * BLOCK_SIZE) % BLOCK_SIZE];
  }
}

/**
 * @brief
 * Assign a new block to an i-Node.
 * @param node An i-Node.
 * @return The assigned block ID.
 */
int inode_assign_new_block(i_node *node) {
  int vac_block = bitmap_find_first_available_block();
  bitmap_occupy_a_block(vac_block);
  if (vac_block == -1) return -1;

  // Try to assign to the direct pointers.
  for (int i = 0; i < 12; ++i)
    if (node->direct_pointers[i] == -1) {
      node->direct_pointers[i] = vac_block;
      return vac_block;
    }

  // Try to assign to indirect blocks;
  int indirect_block[BLOCK_SIZE];
  if (node->indirect_pointer == -1) {
    // If the indirect does not exist.
    node->indirect_pointer = bitmap_find_first_available_block();
    bitmap_occupy_a_block(node->indirect_pointer);
    for (int i = 0; i < BLOCK_SIZE; i++) indirect_block[i] = -1;
    indirect_block[0] = vac_block;
  } else {
    // If the indirect block exists.
    read_blocks(node->indirect_pointer, 1, indirect_block);
    int i = 0;
    for (; i < BLOCK_SIZE - 1 && indirect_block[i + 1] != -1; ++i)
      ;
    if (i == BLOCK_SIZE - 1) return -1;
    indirect_block[i] = vac_block;
  }

  // Flush cached data structures to the disk.
  write_blocks(node->indirect_pointer, 1, indirect_block);
  flush_bitmap();
  flush_i_node_table();
  return vac_block;
}

/**
 * @brief
 * Clear an i-Node completely, which
 * means to restore its initial state
 * and to deallocate all associated
 * memory.
 * @param i_node_id The i-Node ID of the i-Node to clear.
 */
void inode_reset(int i_node_id) {
  i_node node = i_node_table[i_node_id];
  node.mode = 0x777;
  node.gid = -1;
  node.uid = -1;
  node.link_count = 0;
  node.size = -1;

  // Clear the direct pointers.
  for (int i = 0; i < 12 && node.direct_pointers[i] != -1; i++) {
    bitmap_free_a_block(node.direct_pointers[i]);
    node.direct_pointers[i] = -1;
  }

  // Clear indirect pointers.
  if (node.indirect_pointer != -1) {
    int indirect_block[BLOCK_SIZE];
    read_blocks(node.indirect_pointer, 1, indirect_block);

    for (int i = 0; i < BLOCK_SIZE && indirect_block[i] != -1; i++) bitmap_free_a_block(indirect_block[i]);

    node.indirect_pointer = -1;
  }

  // Flush all memory changes to the disk.
  flush_bitmap();
  flush_i_node_table();
}
#pragma endregion

#pragma region FDT Utils
/**
 * @brief
 * Find the first vacant entry in the FDT.
 * @return The ID of the available entry.
 */
int fdt_get_first_available_entry() {
  for (int i = 0; i < NUM_OF_FILES; ++i)
    if (fdt_table[i].i_node_idx == -1) return i;
  return -1;
}
#pragma endregion

#pragma region APIs
/**
 * @brief Initialize Simple File System.
 * If the flag is 1, we create the file system from scratch,
 * in accordance with the default settings.
 *
 * If the flag is 0, we read the super_block from the disk, and
 * initialize the file system accordingly.
 *
 * @param flag Indicate whether to create the file system from scratch.
 */
void mksfs(int flag) {
  if (flag == 1) {
    // Initialize the Simple File System from scratch.
    I_NODE_TABLE_START = 1;
    I_NODE_TABLE_LENGTH = calculate_block_length(NUM_OF_I_NODES * sizeof(i_node));
    ROOT_DIRECTORY_START = I_NODE_TABLE_START + I_NODE_TABLE_LENGTH;
    ROOT_DIRECTORY_LENGTH = calculate_block_length(NUM_OF_FILES * sizeof(directory_entry));
    DATA_BLOCK_START = ROOT_DIRECTORY_START + ROOT_DIRECTORY_LENGTH;
    BITMAP_LENGTH = calculate_block_length(NUM_OF_FILES / 8 + NUM_OF_FILES % 8 > 0);
    BITMAP_START = FILE_SYSTEM_SIZE - BITMAP_LENGTH;
    DATA_BLOCK_LENGTH = BITMAP_START - DATA_BLOCK_START;

    struct super_block super_block = {.block_size = BLOCK_SIZE,
                                      .file_system_size = FILE_SYSTEM_SIZE,
                                      .i_node_num = NUM_OF_I_NODES,
                                      .i_node_table_length = I_NODE_TABLE_LENGTH,
                                      .magic_number = 260917301,
                                      .root_directory = ROOT_DIRECTORY_START};

    // Save the super block.
    char *buffer = write_data_to_a_buffer(&super_block, sizeof(super_block));
    write_blocks(0, 1, buffer);
    free(buffer);

    // Save the initialized iNode table to disk.
    for (int i = 0; i < NUM_OF_I_NODES; i++) {
      i_node_table[i].mode = 0x777;
      i_node_table[i].link_count = 0;
      i_node_table[i].uid = -1;
      i_node_table[i].gid = -1;
      i_node_table[i].size = -1;
      memset(i_node_table->direct_pointers, -1, 12);
      i_node_table[i].indirect_pointer = -1;
    }
    flush_i_node_table();

    // Save the initialized root directory table to the disk.
    for (int i = 0; i < NUM_OF_FILES - 1; i++) {
      root_directory_table[i].i_node_id = -1;
      for (int j = 0; j < MAX_FILE_NAME_LENGTH + MAX_FILE_EXTENSION_LENGTH + 1; j++)
        root_directory_table[i].file_name[j] = '\0';
    }
    flush_root_directory_table();

    // Save the bitmap.
    for (int i = 0; i < FILE_SYSTEM_SIZE / 8; ++i) bitmap[i] = 255;
    for (int i = 0; i < DATA_BLOCK_START; i++) bitmap_occupy_a_block(i);
    for (int i = BITMAP_START; i < FILE_SYSTEM_SIZE; i++) bitmap_occupy_a_block(i);
    flush_bitmap();

    // Initialize the FDT.
    for (int i = 0; i < NUM_OF_FILES; i++) {
      fdt_table[i].i_node_idx = -1;
      fdt_table[i].read_write_pointer = -1;
    }
  } else {
    // The Simple File System is already stored on disk,
    // we should read from the disk to cache necessary
    // contents.
    // Initialize the Simple File System from scratch.
    I_NODE_TABLE_START = 1;
    I_NODE_TABLE_LENGTH = calculate_block_length(NUM_OF_I_NODES * sizeof(i_node));
    ROOT_DIRECTORY_START = I_NODE_TABLE_START + I_NODE_TABLE_LENGTH;
    ROOT_DIRECTORY_LENGTH = calculate_block_length(NUM_OF_FILES * sizeof(directory_entry));
    DATA_BLOCK_START = ROOT_DIRECTORY_START + ROOT_DIRECTORY_LENGTH;
    BITMAP_LENGTH = calculate_block_length(NUM_OF_FILES / 8 + NUM_OF_FILES % 8 > 0);
    BITMAP_START = FILE_SYSTEM_SIZE - BITMAP_LENGTH;
    DATA_BLOCK_LENGTH = BITMAP_START - DATA_BLOCK_START;

    // Read iNode table.
    read_blocks(I_NODE_TABLE_START, I_NODE_TABLE_LENGTH, i_node_table);

    // Read root directory.
    read_blocks(ROOT_DIRECTORY_START, ROOT_DIRECTORY_LENGTH, root_directory_table);

    // Read the bitmap.
    read_blocks(BITMAP_START, BITMAP_LENGTH, bitmap);

    // Initialize the FDT.
    for (int i = 0; i < NUM_OF_FILES; i++) {
      fdt_table[i].i_node_idx = -1;
      fdt_table[i].read_write_pointer = -1;
    }
  }
}

/**
 * @brief Get the name of the next file.
 *
 * @param filename
 * @return int
 */
int sfs_getnextfilename(char *result_buffer) {
  int count_of_files = root_count_number_of_files();
  if (count_of_files == 0)
    return -1;
  else if (files_visited == count_of_files - 1) {
    files_visited = 0;
    char *file_name = root_get_ith_file(0)->file_name;
    memcpy(result_buffer, file_name, strlen(file_name));
    return 1;
  } else {
    char *file_name = root_get_ith_file(files_visited++)->file_name;
    memcpy(result_buffer, file_name, strlen(file_name));
    return 1;
  }
}

/**
 * @brief Get the file size of the specified file.
 *
 * @param filename The filename of the file.
 * @return int The size.
 */
int sfs_getfilesize(const char *filename) {
  directory_entry *result = root_get_directory_entry(filename);
  return result == NULL ? -1 : i_node_table[result->i_node_id].size;
}

/**
 * @brief Open the specified file. If the file does not exist, create the
 * file.
 *
 * @param filename The specified filename.
 * @return int
 */
int sfs_fopen(char *filename) {
  directory_entry *target = root_get_directory_entry(filename);
  if (target != NULL) {
    int vac_fdt = fdt_get_first_available_entry();
    fdt_table[vac_fdt].i_node_idx = (*target).i_node_id;
    fdt_table[vac_fdt].read_write_pointer = 0;
    return vac_fdt;
  } else {
    int vac_root = root_get_first_available_directory_entry(), vac_i_node = inode_tab_get_first_available_entry(),
        vac_fdt = fdt_get_first_available_entry();
    i_node_table[vac_i_node].size = 0;
    root_directory_table[vac_root].i_node_id = vac_i_node;
    memcpy(root_directory_table[vac_root].file_name, filename, strlen(filename));
    fdt_table[vac_fdt].i_node_idx = vac_i_node;
    fdt_table[vac_fdt].read_write_pointer = 0;
    flush_i_node_table();
    flush_root_directory_table();
    return vac_fdt;
  }
}

/**
 * @brief Remove the file from FDT.
 *
 * @param fd The file descriptor.
 * @return int 1 for success and -1 otherwise.
 */
int sfs_fclose(int fd) {
  if (fdt_table[fd].i_node_idx == -1) return -1;
  fdt_table[fd].i_node_idx = -1;
  return 1;
}

int sfs_fwrite(int fd, const char *buf, int length) {
  i_node node = i_node_table[fdt_table[fd].i_node_idx];
  char *buf_cpy = buf;
  int file_size = node.size, ptr = fdt_table[fd].read_write_pointer;
  while (length > 0) {
    int bytes_to_write = length >= BLOCK_SIZE ? BLOCK_SIZE : length;
    int block_id;
    if (ptr >= file_size) {
      block_id = inode_assign_new_block(&node);
      file_size += bytes_to_write;
    } else
      block_id = inode_get_block_id_by_offset(&node, ptr);

    char *b[1024];
    memcpy(b, buf_cpy, bytes_to_write);
    write_blocks(block_id, 1, b);

    buf_cpy += bytes_to_write;
    ptr += bytes_to_write;
    length -= bytes_to_write;
  }
  return 1;
}

int sfs_fread(int fd, char *buf, int length) {
  i_node node = i_node_table[fdt_table[fd].i_node_idx];
  char *buf_cpy = buf;
  int file_size = node.size, ptr = fdt_table[fd].read_write_pointer;
  while (length > 0) {
    int bytes_to_read = file_size - ptr >= BLOCK_SIZE ? BLOCK_SIZE : file_size - ptr;
    int block_id = inode_get_block_id_by_offset(&node, ptr);
    char *block_data[1024];
    read_blocks(block_id, 1, block_data);
    memcpy(buf_cpy, block_data, bytes_to_read);
    buf_cpy += bytes_to_read;
    ptr += bytes_to_read;
    length -= bytes_to_read;
  }
  return 1;
}

/**
 * @brief Adjust the read/write pointer of a file.
 *
 * @param fd The file descriptor.
 * @param loc The new location of the read/write pointer.
 * @return int 1 for success and -1 otherwise.
 */
int sfs_fseek(int fd, int loc) {
  fdt_entry file = fdt_table[fd];
  int i_node_id = file.i_node_idx;
  i_node node = i_node_table[i_node_id];
  if (node.size <= loc) return -1;
  file.read_write_pointer = loc;
  return 1;
}

/**
 * Remove a file from the file system.
 * @param filename Name of the file to remove.
 * @return 1 for success and -1 otherwise.
 */
int sfs_remove(char *filename) {
  if (root_get_directory_entry(filename) != NULL) return -1;
  directory_entry entry = *root_get_directory_entry(filename);
  i_node node = i_node_table[entry.i_node_id];

  // Clear fdt.
  for (int i = 0; i < NUM_OF_FILES; ++i)
    if (fdt_table[i].i_node_idx == entry.i_node_id) {
      fdt_table[i].i_node_idx = -1;
      break;
    }

  // Clear i-Node table.
  i_node_table[entry.i_node_id].size = -1;

  // Clear memory;
  for (int i = 0; i < 12 && node.direct_pointers[i] != -1; ++i) free_a_block_in_bitmap(node.direct_pointers[i]);

  if (node.indirect_pointer != -1) {
    void *buf = (void *)malloc(BLOCK_SIZE);
    read_blocks(node.indirect_pointer, 1, buf);
    int array_of_pointers[BLOCK_SIZE];
    memcpy(array_of_pointers, buf, BLOCK_SIZE);
    free(buf);
    for (int i = 0; i < BLOCK_SIZE && array_of_pointers[i] != -1; ++i) free_a_block_in_bitmap(array_of_pointers[i]);
  }

  flush_i_node_table();
  flush_root_directory_table();

  return 1;
}
#pragma endregion