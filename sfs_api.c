#include "sfs_api.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// --------------------------------------------------------------------
// ----------------------------- CONSTANTS ----------------------------
// --------------------------------------------------------------------
#define MAX_FILE_NAME_LENGTH 16
#define MAX_FILE_EXTENSION_LENGTH 3
#define BLOCK_SIZE 1024
#define FILE_SYSTEM_SIZE 1024
#define NUM_OF_I_NODES 200
#define NUM_OF_FILES NUM_OF_I_NODES
unsigned int I_NODE_TABLE_START = 1;
unsigned int I_NODE_TABLE_LENGTH;
unsigned int ROOT_DIRECTORY_START;
unsigned int ROOT_DIRECTORY_LENGTH;
unsigned int DATA_BLOCK_START;
unsigned int DATA_BLOCK_LENGTH;
unsigned int BITMAP_START;
unsigned int BITMAP_LENGTH;

// --------------------------------------------------------------------
// ------------------------- BASIC DATA TYPES -------------------------
// --------------------------------------------------------------------

typedef struct superblock {
    unsigned int magic_number;        // This is a magic number.
    unsigned int block_size;          // The size of each block, in bytes.
    unsigned int file_system_size;    // The total number of blocks the disk has.
    unsigned int i_node_table_length; // The number of blocks to contain all i-nodes.
    unsigned int i_node_num;          // The number of i-nodes we have in this disk.
    unsigned int root_directory;      // The pointer to the i-node for the root directory.
} superblock;

typedef struct i_node {
    unsigned int mode; // The mode to operate on this file.
    unsigned int link_count;
    unsigned int uid;
    unsigned int gid;
    unsigned int size;                // The size of the file.
    unsigned int direct_pointers[12]; // 12 direct pointers, each pointing to a data block.
    unsigned int indirect_pointer;    // One indirect pointer, which points to a block containing references to subsequent blocks.
} i_node;

typedef struct directory_entry {
    unsigned int i_node_id; // The i-Node this directory points to.
    char file_name[MAX_FILE_NAME_LENGTH + MAX_FILE_EXTENSION_LENGTH + 1]
} directory_entry;

typedef struct fdt_entry {
    unsigned int i_node_idx;        // The idx of the i-Node this file points to.
    unsigned int read_write_pointer // The read/write pointer of this file.
} fdt_entry;

// --------------------------------------------------------------------
// ------------------------------- UTILS ------------------------------
// --------------------------------------------------------------------
/**
 * @brief Calculate the number of blocks necessary to store the file.
 *
 * @param file_size File size.
 * @return unsigned int The number of blocks.
 */
unsigned int calculate_block_length(unsigned int file_size) { return file_size / BLOCK_SIZE + (file_size % BLOCK_SIZE) > 0; }

/**
 * @brief Set the specified bit to 1.
 *
 * @param record The byte to operate on.
 * @param bit_idx The index of the bit, with the leftmost bit being 0 and the rightmost being 7.
 * @return true If successfull.
 * @return false Otherwise.
 */
bool assign_1_to_bit(char *record, int bit_idx) {}

/**
 * @brief Set the specified bit to 0.
 *
 * @param record The byte to operate on.
 * @param bit_idx The index of the bit, with the leftmost bit being 0 and the rightmost being 7.
 * @return true If successfull.
 * @return false Otherwise.
 */
bool assign_0_to_bit(char *record, int bit_idx) {}

/**
 * @brief Determines if the specified bit is 1.
 *
 * @param record The byte of interest.
 * @param bit_idx The specified index of the bit.
 * @return true If the specified bit is 1.
 * @return false If it is 0.
 */
bool is_bit_1(char *record, int bit_idx) {}

/**
 * @brief Flush the cached i_Node table to disk.
 *
 * @return true If successful.
 * @return false If failed.
 */
bool flush_i_node_table() {}

/**
 * @brief Flush the root directory table to the disk.
 *
 * @return true If successful.
 * @return false If failed.
 */
bool flush_root_directory_table() {}

/**
 * @brief Flush the cached bitmap to the disk.
 *
 * @return true If successful.
 * @return false If failed.
 */
bool flush_bit_map() {}

/**
 * @brief Find the first empty block given that range, update the bitmap, and then flush the cached bitmap to disk.
 *
 * @param search_start The first block to search.
 * @param search_end The last block to search.
 * @return int Returns -1 if no empty blocks, and returns the block id if found.
 */
int allocate_a_block(unsigned int search_start, unsigned int search_end) {}

/**
 * @brief Free the specified block by erasing it from the bitmap, and then flush the bitmap to the disk.
 *
 * @param block_id The ID of the block to free.
 * @return Returns true if successful, false otherwise.
 */
bool free_a_block(unsigned int block_id) {}

/**
 * @brief Initialize relevant constants given the superblock info.
 *
 * @param superblock The superblock used to initialize constants.
 */
void initialize_constants(superblock *superblock) {
    if (superblock == NULL) {
        // Initialize from scratch.
        I_NODE_TABLE_START = 1;
        I_NODE_TABLE_LENGTH = calculate_block_length(NUM_OF_I_NODES * sizeof(i_node));
        ROOT_DIRECTORY_START = I_NODE_TABLE_START + I_NODE_TABLE_LENGTH;
        ROOT_DIRECTORY_LENGTH = calculate_block_length(NUM_OF_FILES * sizeof(directory_entry));
        DATA_BLOCK_START = ROOT_DIRECTORY_START + ROOT_DIRECTORY_LENGTH;
        BITMAP_LENGTH = calculate_block_length(NUM_OF_FILES / 8 + NUM_OF_FILES % 8 > 0);
        BITMAP_START = FILE_SYSTEM_SIZE - BITMAP_LENGTH;
        DATA_BLOCK_LENGTH = BITMAP_START - DATA_BLOCK_START;
    } else {
        // Initialize according to the superblock.
    }
}

// --------------------------------------------------------------------
// ------------------------------ APIs --------------------------------
// --------------------------------------------------------------------
