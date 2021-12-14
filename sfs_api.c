#include "sfs_api.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "disk_emu.h"

#define MAX_FILE_NAME_LENGTH 16
#define MAX_FILE_EXTENSION_LENGTH 3
#define FILE_SYSTEM_SIZE 1024
#define FILE_SYSTEM_BLOCK_SIZE 1024
#define NUM_OF_I_NODES 200
#define NUM_OF_FILES (NUM_OF_I_NODES - 1)
#define VERBOSE true

#pragma region Some Output Colors
// Regular text
#define BLK "\e[0;30m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define MAG "\e[0;35m"
#define CYN "\e[0;36m"
#define WHT "\e[0;37m"

// Regular bold text
#define BBLK "\e[1;30m"
#define BRED "\e[1;31m"
#define BGRN "\e[1;32m"
#define BYEL "\e[1;33m"
#define BBLU "\e[1;34m"
#define BMAG "\e[1;35m"
#define BCYN "\e[1;36m"
#define BWHT "\e[1;37m"

// Regular underline text
#define UBLK "\e[4;30m"
#define URED "\e[4;31m"
#define UGRN "\e[4;32m"
#define UYEL "\e[4;33m"
#define UBLU "\e[4;34m"
#define UMAG "\e[4;35m"
#define UCYN "\e[4;36m"
#define UWHT "\e[4;37m"

// Regular background
#define BLKB "\e[40m"
#define REDB "\e[41m"
#define GRNB "\e[42m"
#define YELB "\e[43m"
#define BLUB "\e[44m"
#define MAGB "\e[45m"
#define CYNB "\e[46m"
#define WHTB "\e[47m"

// High intensty background
#define BLKHB "\e[0;100m"
#define REDHB "\e[0;101m"
#define GRNHB "\e[0;102m"
#define YELHB "\e[0;103m"
#define BLUHB "\e[0;104m"
#define MAGHB "\e[0;105m"
#define CYNHB "\e[0;106m"
#define WHTHB "\e[0;107m"

// High intensty text
#define HBLK "\e[0;90m"
#define HRED "\e[0;91m"
#define HGRN "\e[0;92m"
#define HYEL "\e[0;93m"
#define HBLU "\e[0;94m"
#define HMAG "\e[0;95m"
#define HCYN "\e[0;96m"
#define HWHT "\e[0;97m"

// Bold high intensity text
#define BHBLK "\e[1;90m"
#define BHRED "\e[1;91m"
#define BHGRN "\e[1;92m"
#define BHYEL "\e[1;93m"
#define BHBLU "\e[1;94m"
#define BHMAG "\e[1;95m"
#define BHCYN "\e[1;96m"
#define BHWHT "\e[1;97m"

// Reset
#define reset "\e[0m"

#pragma endregion

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
i_node g_inode_table[NUM_OF_I_NODES];
fdt_entry g_fdt[NUM_OF_FILES];
directory_entry g_root_directory_table[NUM_OF_FILES];
unsigned char g_bitmap[FILE_SYSTEM_SIZE / 8];

int root_file_counter = 0;

#pragma region General Utils
/**
 * @brief
 * Calculate the number of blocks necessary
 * for the specified file size.
 *
 * @param file_size The file size.
 * @return unsigned int The number of blocks.
 */

int calculate_block_length(int file_size) {
  // Note that the result depends on the block size.
  int a = file_size / FILE_SYSTEM_BLOCK_SIZE;
  int b = file_size % FILE_SYSTEM_BLOCK_SIZE > 0 ? 1 : 0;
  return a + b;
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
  unsigned int total_mem_size = num_of_blocks_needed * FILE_SYSTEM_BLOCK_SIZE;
  void *buffer = (void *)malloc(total_mem_size);
  memset(buffer, 0, total_mem_size);
  memcpy(buffer, data, data_size);
  return buffer;
}

int min(int a, int b) { return a <= b ? a : b; }

int max(int a, int b) { return a >= b ? a : b; }

/**
 * @brief
 * Print errors to stdout.
 * @param msg The msg to print.
 */
void print_error(char *msg) {
  if (VERBOSE) {
    printf(BRED "[SFS ERROR] " reset "%s " BBLU "TIMESTAMP: " reset "%lu\n", msg, time(NULL));
  }
}
#pragma endregion

#pragma region Bitmap Utils
/**
 * @brief
 * Check whether the specified block is
 * available according to the g_bitmap.
 * @param block_id The ID of the block to investigate.
 * @return True If free.
 * @return False If occupied.
 */
bool bitmap_is_block_free(int block_id) {
  int row_num = block_id / 8, column_num = block_id % 8;
  return (g_bitmap[row_num] & (1 << column_num)) > 0;
}

/**
 * @brief
 * Free a block in the g_bitmap
 * by setting it to 1.
 * @param block_id The ID of the block to free.
 */
void bitmap_free_a_block(int block_id) {
  int row_num = block_id / 8, column_num = block_id % 8;
  g_bitmap[row_num] |= (1 << column_num);
}

/**
 * @brief
 * Occupy a block in the g_bitmap
 * by setting it to 0.
 * @param block_id The ID of the block to occupy.
 */
void bitmap_occupy_a_block(int block_id) {
  int row_num = block_id / 8, column_num = block_id % 8;
  g_bitmap[row_num] &= ~(1 << column_num);
}

/**
 * @brief
 * Find the first available block
 * according to the g_bitmap.
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
  void *buffer = write_data_to_a_buffer(g_inode_table, NUM_OF_I_NODES * sizeof(i_node));
  write_blocks(I_NODE_TABLE_START, I_NODE_TABLE_LENGTH, buffer);
  free(buffer);
}

/**
 * @brief
 * Flush the root directory table to the disk.
 */
void flush_root_directory_table() {
  void *buffer = write_data_to_a_buffer(g_root_directory_table, NUM_OF_FILES * sizeof(directory_entry));
  write_blocks(ROOT_DIRECTORY_START, ROOT_DIRECTORY_LENGTH, buffer);
}

/**
 * @brief
 * Flush the cached g_bitmap to the disk.
 */
void flush_bitmap() {
  void *buffer = write_data_to_a_buffer(g_bitmap, FILE_SYSTEM_SIZE / 8);
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
    if (g_root_directory_table[i].i_node_id != -1 && strcmp(g_root_directory_table[i].file_name, filename) == 0)
      return &g_root_directory_table[i];
  return NULL;
}

/**
 * @brief
 * Get the directory entry of a file
 * given its inode.
 * @param inode_idx The iNode ID.
 * @return Its directory entry.
 */
directory_entry *root_get_directory_entry_via_inode(int inode_idx) {
  for (int i = 0; i < NUM_OF_FILES; i++)
    if (g_root_directory_table[i].i_node_id == inode_idx) return &g_root_directory_table[i];
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
    if (g_root_directory_table[i].i_node_id != -1) result++;
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
    if (g_root_directory_table[j].i_node_id != -1 && count++ == i) return &g_root_directory_table[i];
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
    if (g_root_directory_table[i].i_node_id == -1) return i;
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
    if (g_inode_table[i].size == -1) return i;
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
  int indirect_block[FILE_SYSTEM_BLOCK_SIZE];

  if ((calculate_block_length(file_size) * FILE_SYSTEM_BLOCK_SIZE) <= loc) {
    print_error("Trying to access memory that does not belong to the file.");
    return -1;
  } else if (loc < 12 * FILE_SYSTEM_BLOCK_SIZE)
    // The block should be in the direct pointers.
    return node->direct_pointers[loc / FILE_SYSTEM_BLOCK_SIZE];
  else {
    // The block should be in the indirect pointers.
    read_blocks(indirect_ptr, 1, indirect_block);
    return indirect_block[(loc - 12 * FILE_SYSTEM_BLOCK_SIZE) / FILE_SYSTEM_BLOCK_SIZE];
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
  int indirect_block[FILE_SYSTEM_BLOCK_SIZE];
  if (node->indirect_pointer == -1) {
    // If the indirect does not exist.
    node->indirect_pointer = bitmap_find_first_available_block();
    bitmap_occupy_a_block(node->indirect_pointer);
    for (int i = 0; i < FILE_SYSTEM_BLOCK_SIZE; i++) indirect_block[i] = -1;
    indirect_block[0] = vac_block;
  } else {
    // If the indirect block exists.
    read_blocks(node->indirect_pointer, 1, indirect_block);
    int i = 0;
    for (; i < FILE_SYSTEM_BLOCK_SIZE - 1 && indirect_block[i + 1] != -1; ++i)
      ;
    if (i == FILE_SYSTEM_BLOCK_SIZE - 1) {
      print_error("The file has consumed all available iNode space.");
      return -1;
    }
    indirect_block[i + 1] = vac_block;
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
  i_node node = g_inode_table[i_node_id];
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
    int indirect_block[FILE_SYSTEM_BLOCK_SIZE];
    read_blocks(node.indirect_pointer, 1, indirect_block);

    for (int i = 0; i < FILE_SYSTEM_BLOCK_SIZE && indirect_block[i] != -1; i++) bitmap_free_a_block(indirect_block[i]);

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
    if (g_fdt[i].i_node_idx == -1) return i;
  return -1;
}

/**
 * @brief
 * Determine if the file has already
 * been opened. If it has, then simple
 * return its file descriptor, otherwise
 * return -1.
 * @param filename The filename.
 * @return fd, if opened.
 * @return -1, otherwise.
 */
int fdt_get_fd_by_filename(const char *filename) {
  for (int i = 0; i < NUM_OF_FILES; i++)
    if (g_fdt[i].i_node_idx != -1) {
      int inode_idx = g_fdt[i].i_node_idx;
      if (strcmp(root_get_directory_entry_via_inode(inode_idx)->file_name, filename) == 0) return i;
    }
  return -1;
}
#pragma endregion

#pragma region APIs
/**
 * @brief
 * Initialize Simple File System.
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
    BITMAP_LENGTH = calculate_block_length(NUM_OF_FILES / 8 + (NUM_OF_FILES % 8 > 0 ? 1 : 0));
    BITMAP_START = FILE_SYSTEM_SIZE - BITMAP_LENGTH;
    DATA_BLOCK_LENGTH = BITMAP_START - DATA_BLOCK_START;

    // Init a fresh disk.
    init_fresh_disk("sfs.txt", FILE_SYSTEM_BLOCK_SIZE, FILE_SYSTEM_SIZE);

    struct super_block super_block = {.block_size = FILE_SYSTEM_BLOCK_SIZE,
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
      g_inode_table[i].mode = 0x777;
      g_inode_table[i].link_count = 0;
      g_inode_table[i].uid = -1;
      g_inode_table[i].gid = -1;
      g_inode_table[i].size = -1;
      for (int j = 0; j < 12; ++j) g_inode_table[i].direct_pointers[j] = -1;
      g_inode_table[i].indirect_pointer = -1;
    }

    // Initialize root iNode.
    struct i_node *r = &g_inode_table[0];
    r->size = NUM_OF_FILES * sizeof(directory_entry);
    for (int i = 0; i < ROOT_DIRECTORY_LENGTH; i++) r->direct_pointers[i] = i + ROOT_DIRECTORY_START;

    flush_i_node_table();

    // Save the initialized root directory table to the disk.
    for (int i = 0; i < NUM_OF_FILES - 1; i++) {
      g_root_directory_table[i].i_node_id = -1;
      for (int j = 0; j < MAX_FILE_NAME_LENGTH + MAX_FILE_EXTENSION_LENGTH + 1; j++)
        g_root_directory_table[i].file_name[j] = '\0';
    }
    flush_root_directory_table();

    // Save the g_bitmap.
    for (int i = 0; i < FILE_SYSTEM_SIZE / 8; ++i) g_bitmap[i] = 255;
    for (int i = 0; i < DATA_BLOCK_START; i++) bitmap_occupy_a_block(i);
    for (int i = BITMAP_START; i < FILE_SYSTEM_SIZE; i++) bitmap_occupy_a_block(i);
    flush_bitmap();

    // Initialize the FDT.
    for (int i = 0; i < NUM_OF_FILES; i++) {
      g_fdt[i].i_node_idx = -1;
      g_fdt[i].read_write_pointer = -1;
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

    // Init an existing disk
    init_disk("sfs.txt", FILE_SYSTEM_BLOCK_SIZE, FILE_SYSTEM_SIZE);

    // Read iNode table.
    read_blocks(I_NODE_TABLE_START, I_NODE_TABLE_LENGTH, g_inode_table);

    // Read root directory.
    read_blocks(ROOT_DIRECTORY_START, ROOT_DIRECTORY_LENGTH, g_root_directory_table);

    // Read the g_bitmap.
    read_blocks(BITMAP_START, BITMAP_LENGTH, g_bitmap);

    // Initialize the FDT.
    for (int i = 0; i < NUM_OF_FILES; i++) {
      g_fdt[i].i_node_idx = -1;
      g_fdt[i].read_write_pointer = -1;
    }
  }
}

/**
 * @brief
 * Get the name of the next file.
 *
 * @param result_buffer The buffer to which the method writes the result.
 * @return 1, if success.
 * @return -1, otherwise.
 */
int sfs_getnextfilename(char *result_buffer) {
  int count_of_files = root_count_number_of_files();
  if (count_of_files == 0)
    return -1;
  else if (root_file_counter == count_of_files) {
    root_file_counter = 0;
    char *file_name = root_get_ith_file(0)->file_name;
    memcpy(result_buffer, file_name, strlen(file_name));
    return 1;
  } else {
    char *file_name = root_get_ith_file(root_file_counter++)->file_name;
    memcpy(result_buffer, file_name, strlen(file_name));
    return 1;
  }
}

/**
 * @brief
 * Get the file size of the specified file.
 *
 * @param filename The filename.
 * @return int, the file size if the file exists.
 * @return -1, if failed.
 */
int sfs_getfilesize(const char *filename) {
  directory_entry *result = root_get_directory_entry(filename);
  return result == NULL ? -1 : g_inode_table[result->i_node_id].size;
}

/**
 * @brief
 * If the file exists, bring it into the FDT.
 * If the file does not exist, create the file.
 *
 * @param filename The filename.
 * @return 1, if success.
 * @return 0, otherwise.
 */
int sfs_fopen(char *filename) {
  // Search through the FDT.
  // If the file has already been opened,
  // simply return its file descriptor.
  int fd = fdt_get_fd_by_filename(filename);
  if (fd != -1) return fd;

  directory_entry *target = root_get_directory_entry(filename);
  if (target != NULL) {
    // The file exists in the root.
    int vac_fdt = fdt_get_first_available_entry();

    // If the FDT is full.
    if (vac_fdt == -1) {
      char msg[1000];
      sprintf(msg, "Cannot open file '%s' because the FDT is full.", filename);
      print_error(msg);
      return -1;
    }

    g_fdt[vac_fdt].i_node_idx = (*target).i_node_id;
    g_fdt[vac_fdt].read_write_pointer = g_inode_table[target->i_node_id].size;
    return vac_fdt;
  } else {
    // The file does not exist, and we shall create it.
    int vac_root = root_get_first_available_directory_entry();
    int vac_i_node = inode_tab_get_first_available_entry();
    int vac_fdt = fdt_get_first_available_entry();

    // If there is no available resources.
    if (vac_root == -1 || vac_i_node == -1 || vac_fdt == -1) {
      char msg[1000];
      sprintf(msg, "Cannot open file '%s' because either the root, the iNode table, or the fdt is full.", filename);
      print_error(msg);
      return -1;
    }

    g_inode_table[vac_i_node].size = 0;
    g_root_directory_table[vac_root].i_node_id = vac_i_node;
    memcpy(g_root_directory_table[vac_root].file_name, filename, strlen(filename));
    g_fdt[vac_fdt].i_node_idx = vac_i_node;
    g_fdt[vac_fdt].read_write_pointer = 0;
    flush_i_node_table();
    flush_root_directory_table();
    return vac_fdt;
  }
}

/**
 * @brief
 * Remove the file from the FDT.
 *
 * @param fd The file descriptor.
 * @return 1, if success.
 * @return -1, otherwise.
 */
int sfs_fclose(int fd) {
  if (g_fdt[fd].i_node_idx == -1) {
    print_error("Attempt to close a file that has already been closed.");
    return -1;
  }

  g_fdt[fd].i_node_idx = -1;
  g_fdt[fd].read_write_pointer = -1;
  return 0;
}

/**
 * @brief
 * Write to a file.
 * @param fd The file descriptor.
 * @param buf The buffer to write.
 * @param length The length of the message.
 * @return number of bytes written, if successful.
 * @return -1, otherwise.
 */
int sfs_fwrite(int fd, const char *buf, int length) {
  // If the file has not been opened.
  if (g_fdt[fd].i_node_idx == -1) {
    print_error("Cannot write to a file that is not opened.");
    return -1;
  }

  i_node *node = &g_inode_table[g_fdt[fd].i_node_idx];
  char *buf_cpy = buf;
  int file_size = node->size, ptr = g_fdt[fd].read_write_pointer, total_bytes_written = 0;
  while (length > 0) {
    int bytes_to_write = min(FILE_SYSTEM_BLOCK_SIZE - ptr % FILE_SYSTEM_BLOCK_SIZE,
                             length >= FILE_SYSTEM_BLOCK_SIZE ? FILE_SYSTEM_BLOCK_SIZE : length);
    int block_id;

    if (ptr % FILE_SYSTEM_BLOCK_SIZE == 0 && ptr >= file_size) {
      // We are running out of blocks, assign new one.
      block_id = inode_assign_new_block(node);
      char b[FILE_SYSTEM_BLOCK_SIZE];
      memcpy(b, buf_cpy, bytes_to_write);
      write_blocks(block_id, 1, b);
      file_size += bytes_to_write;
    } else {
      // Simple get the designate block.
      block_id = inode_get_block_id_by_offset(node, ptr);
      char b[FILE_SYSTEM_BLOCK_SIZE];
      read_blocks(block_id, 1, b);
      memcpy(b + ptr % FILE_SYSTEM_BLOCK_SIZE, buf_cpy, bytes_to_write);
      write_blocks(block_id, 1, b);
      file_size = max(file_size, ptr + bytes_to_write);
    }

    buf_cpy += bytes_to_write;
    ptr += bytes_to_write;
    length -= bytes_to_write;
    total_bytes_written += bytes_to_write;
  }

  node->size = file_size;
  flush_bitmap();
  flush_i_node_table();
  g_fdt[fd].read_write_pointer = ptr;

  return total_bytes_written;
}

/**
 * @brief
 * Read messages from the file.
 * @param fd The file descriptor.
 * @param buf The buffer to which the message is written.
 * @param length The length of the message to read.
 * @return 1, if success.
 * @return 0, otherwise.
 */
int sfs_fread(int fd, char *buf, int length) {
  // If the file has not been opened.
  if (g_fdt[fd].i_node_idx == -1) {
    print_error("Cannot write to a file that is not opened.");
    return -1;
  }

  i_node *node = &g_inode_table[g_fdt[fd].i_node_idx];
  char *buf_cpy = buf;
  int ptr = g_fdt[fd].read_write_pointer, total_bytes_read = 0;
  while (length > 0 && ptr < node->size) {
    int bytes_to_read = min(FILE_SYSTEM_BLOCK_SIZE - ptr % FILE_SYSTEM_BLOCK_SIZE,
                            length >= FILE_SYSTEM_BLOCK_SIZE ? FILE_SYSTEM_BLOCK_SIZE : length);

    int block_id = inode_get_block_id_by_offset(node, ptr);
    char block_data[1024];
    read_blocks(block_id, 1, block_data);
    memcpy(buf_cpy, block_data + ptr % FILE_SYSTEM_BLOCK_SIZE, bytes_to_read);
    buf_cpy += bytes_to_read;
    ptr += bytes_to_read;
    length -= bytes_to_read;
    total_bytes_read += bytes_to_read;
  }

  g_fdt[fd].read_write_pointer = ptr;

  return total_bytes_read;
}

/**
 * @brief
 * Adjust the read/write pointer of a file.
 *
 * @param fd The file descriptor.
 * @param loc The new location of the read/write pointer.
 * @return 1, if success
 * @return -1, otherwise.
 */
int sfs_fseek(int fd, int loc) {
  fdt_entry *file = &g_fdt[fd];

  // If the fd is invalid.
  if (file->i_node_idx == -1) {
    print_error("The file to seek has not been opened.");
    return -1;
  }

  i_node node = g_inode_table[file->i_node_idx];
  int size = node.size;

  // If the location exceeds the file size.
  if (loc >= size) {
    print_error("The 'loc' variable has exceeded the file size.");
    return -1;
  }

  file->read_write_pointer = loc;
  return 1;
}

/**
 * @brief
 * Delete a file.
 * @param filename Name of the file to remove.
 * @return 1, if success.
 * @return -1, otherwise.
 */
int sfs_remove(char *filename) {
  directory_entry *root_entry = root_get_directory_entry(filename);

  // If the file to delete does not exist.
  if (root_entry == NULL) return -1;

  int inode_id = root_entry->i_node_id;

  // Clear fdt.
  for (int i = 0; i < NUM_OF_FILES; ++i)
    if (g_fdt[i].i_node_idx == inode_id) {
      g_fdt[i].i_node_idx = -1;
      g_fdt[i].read_write_pointer = -1;
      break;
    }

  // Clear i-Node table.
  inode_reset(inode_id);

  // Clear root directory.
  root_entry->i_node_id = -1;
  memset(root_entry->file_name, '\0', MAX_FILE_EXTENSION_LENGTH + MAX_FILE_NAME_LENGTH + 1);

  flush_root_directory_table();

  return 1;
}
#pragma endregion