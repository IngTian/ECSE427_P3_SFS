#include "sfs_api.h"
#include "disk_emu.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// Cached variables.
i_node i_node_table[NUM_OF_I_NODES];
fdt_entry fdt_table[NUM_OF_FILES];
directory_entry root_directory_table[NUM_OF_FILES];
char bitmap[FILE_SYSTEM_SIZE / 8];

unsigned int files_visited = 0;
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
 * @param bit_idx The index of the bit, with the leftmost bit being 7 and the rightmost being 0.
 * @return true If successfull.
 * @return false Otherwise.
 */
void assign_1_to_bit(char *record, int bit_idx) { *record = *record | (1 << bit_idx); }

/**
 * @brief Set the specified bit to 0.
 *
 * @param record The byte to operate on.
 * @param bit_idx The index of the bit, with the leftmost bit being 7 and the rightmost being 0.
 * @return true If successfull.
 * @return false Otherwise.
 */
void assign_0_to_bit(char *record, int bit_idx) { *record = *record & ~(1 << bit_idx); }

/**
 * @brief Determines if the specified bit is 1.
 *
 * @param record The byte of interest.
 * @param bit_idx The specified index of the bit.
 * @return true If the specified bit is 1.
 * @return false If it is 0.
 */
bool is_bit_1(char *record, int bit_idx) { return *record & (1 << bit_idx) > 0; }

/**
 * @brief Writes the data to a buffer.
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

/**
 * @brief Flush the cached i_Node table to disk.
 */
void flush_i_node_table() {
    void *buffer = write_data_to_a_buffer(i_node_table, NUM_OF_I_NODES * sizeof(i_node));
    write_blocks(I_NODE_TABLE_START, I_NODE_TABLE_LENGTH, buffer);
    free(buffer);
}

/**
 * @brief Flush the root directory table to the disk.
 */
void flush_root_directory_table() {
    void *buffer = write_data_to_a_buffer(root_directory_table, NUM_OF_FILES * sizeof(directory_entry));
    write_blocks(ROOT_DIRECTORY_START, ROOT_DIRECTORY_LENGTH, buffer);
    free(buffer);
}

/**
 * @brief Flush the cached bitmap to the disk.
 */
void flush_bit_map() {
    void *buffer = write_data_to_a_buffer(bitmap, FILE_SYSTEM_SIZE / 8);
    write_blocks(BITMAP_START, BITMAP_LENGTH, buffer);
    free(buffer);
}

/**
 * @brief Find the first empty block given that range, update the bitmap, and then flush the cached bitmap to disk.
 *
 * @param search_start The first block to search.
 * @param search_end The last block to search.
 * @return int Returns -1 if no empty blocks, and returns the block id if found.
 */
int allocate_a_block_in_bitmap(unsigned int search_start, unsigned int search_end) {
    int i = search_start;
    for (i; i <= search_end; i++) {
        if (bitmap[i / 8] == 0 || !is_bit_1(&bitmap[i / 8], i % 8))
            continue;
        assign_0_to_bit(&bitmap[i / 8], i % 8);
        flush_bit_map();
        return i;
    }
    return -1;
}

/**
 * @brief Free the specified block by erasing it from the bitmap, and then flush the bitmap to the disk.
 *
 * @param block_id The ID of the block to free.
 */
void free_a_block_in_bitmap(unsigned int block_id) {
    assign_1_to_bit(&bitmap[block_id / 8], block_id % 8);
    flush_bit_map();
}

// --------------------------------------------------------------------
// ------------------------- API-Specific Utils -----------------------
// --------------------------------------------------------------------

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

/**
 * @brief Initialize Simple File System.
 * If the flag is 1, we create the file system from scratch,
 * in accordance with the default settings.
 *
 * If the flag is 0, we read the superblock from the disk, and
 * initialize the file system accordingly.
 *
 * @param flag Indicate whether to create the file system from scratch.
 */
void mksfs(int flag) {}

/**
 * @brief Get the name of the next file.
 *
 * @param filename
 * @return int
 */
int sfs_getnextfilename(char *result_buffer) {
    int traversed_files = 0;
    directory_entry *first_non_empty_entry = NULL;
    for (int i = 0; i < NUM_OF_FILES; i++) {
        directory_entry current_entry = root_directory_table[i];
        if (current_entry.i_node_id == -1)
            continue;

        if (traversed_files == 0)
            first_non_empty_entry = &current_entry;

        if (traversed_files++ == files_visited) {
            memcpy(result_buffer, current_entry.file_name, sizeof(current_entry.file_name));
            files_visited++;
            return 1;
        }
    }

    // If there is no first_non_empty entry, there is essentially no files in the root directory.
    if (first_non_empty_entry == NULL)
        return -1;
    else {
        memcpy(result_buffer, first_non_empty_entry->file_name, sizeof(first_non_empty_entry->file_name));
        files_visited++;
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
    for (int i = 0; i < NUM_OF_FILES; i++) {
        directory_entry cur_entry = root_directory_table[i];
        char *cur_file_name = cur_entry.file_name;
        unsigned int i_node_id = cur_entry.i_node_id;
        if (i_node_id == -1 || strcmp(filename, cur_file_name) != 0)
            continue;
        i_node node = i_node_table[i_node_id];
        return node.size;
    }
    return -1;
}

int sfs_fopen(char *filename) {}

int sfs_fclose(int fd) {}

int sfs_fwrite(int fd, const char *buf, int length) {}

int sfs_fread(int fd, char *buf, int length) {}

int sfs_fseek(int fd, int loc) {}

int sfs_remove(char *filename) {}
