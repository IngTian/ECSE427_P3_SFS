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

// --------------------------------------------------------------------
// ------------------------- BASIC DATA TYPES -------------------------
// --------------------------------------------------------------------

typedef struct super_block {
    int magic_number;        // This is a magic number.
    int block_size;          // The size of each block, in bytes.
    int file_system_size;    // The total number of blocks the disk has.
    int i_node_table_length; // The number of blocks to contain all i-nodes.
    int i_node_num;          // The number of i-nodes we have in this disk.
    int root_directory;      // The pointer to the i-node for the root directory.
} super_block;

typedef struct i_node {
    int mode; // The mode to operate on this file.
    int link_count;
    int uid;
    int gid;
    int size;                // The size of the file.
    int direct_pointers[12]; // 12 direct pointers, each pointing to a data block.
    int indirect_pointer;    // One indirect pointer, which points to a block containing references to subsequent blocks.
} i_node;

typedef struct directory_entry {
    int i_node_id; // The i-Node this directory points to.
    char file_name[MAX_FILE_NAME_LENGTH + MAX_FILE_EXTENSION_LENGTH + 1];
} directory_entry;

typedef struct fdt_entry {
    int i_node_idx;         // The idx of the i-Node this file points to.
    int read_write_pointer; // The read/write pointer of this file.
} fdt_entry;

// --------------------------------------------------------------------
// ------------------------- GLOBAL VARIABLES -------------------------
// --------------------------------------------------------------------

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

// --------------------------------------------------------------------
// ------------------------------- UTILS ------------------------------
// --------------------------------------------------------------------
/**
 * @brief Calculate the number of blocks necessary to store the file.
 *
 * @param file_size File size.
 * @return unsigned int The number of blocks.
 */
int calculate_block_length(unsigned int file_size) { return file_size / BLOCK_SIZE + (file_size % BLOCK_SIZE) > 0; }

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
bool is_bit_1(const char *record, int bit_idx) { return (*record & (1 << bit_idx)) > 0; }

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
int allocate_a_block_in_bitmap(int search_start, int search_end) {
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
void free_a_block_in_bitmap(int block_id) {
    assign_1_to_bit(&bitmap[block_id / 8], block_id % 8);
    flush_bit_map();
}

/**
 * Clear a block to 0.
 * @param block_id The block id of the given block.
 */
void clear_a_block(int block_id) {
    char temp[BLOCK_SIZE];
    memset(temp, 0, BLOCK_SIZE);
    write_blocks(block_id, 1, temp);
}

// --------------------------------------------------------------------
// ------------------------- API-Specific Utils -----------------------
// --------------------------------------------------------------------
/**
 * @brief Get the directory entry of the given filename.
 * @param filename The name of file to retrieve.
 * @return The pointer to the specified file entry.
 */
directory_entry *get_file_in_root(const char *filename) {
    for (int i = 0; i < NUM_OF_FILES; i++)
        if (root_directory_table[i].i_node_id != -1 && strcmp(root_directory_table[i].file_name, filename) == 0)
            return &root_directory_table[i];

    return NULL;
}

/**
 * Get the total number of files under the root directory.
 * @return The counts.
 */
int count_files_in_root() {
    int result = 0;
    for (int i = 0; i < NUM_OF_FILES; i++)
        if (root_directory_table[i].i_node_id != -1)
            result++;
    return result;
}

/**
 * Get the ith directory entry in the root.
 * @param i Index.
 * @return The ith directory entry.
 */
directory_entry *get_ith_file_in_root(int i) {
    int count = 0;
    for (int j = 0; j < NUM_OF_FILES; ++j)
        if (root_directory_table[j].i_node_id != -1 && count++ == i)
            return &root_directory_table[i];
    return NULL;
}

/**
 * Find the first spot that is vacant in the root directory.
 * @return The idx to the spot.
 */
int get_root_first_vacant_spot() {
    for (int i = 0; i < NUM_OF_FILES; i++)
        if (root_directory_table[i].i_node_id == -1)
            return i;
    return -1;
}

/**
 * Get the first vacant i-Node spot.
 * @return
 */
int get_i_node_first_vacant_spot() {
    for (int i = 0; i < NUM_OF_I_NODES; ++i)
        if (i_node_table[i].size != -1)
            return i;
    return -1;
}

int get_block_id_by_ptr(i_node *node, int loc) {
    int file_size = node->size;
    int indirect_ptr = node->indirect_pointer;
    int indirect_block[BLOCK_SIZE];
    if (indirect_ptr != -1)
        write_blocks(indirect_ptr, 1, indirect_block);
    if (loc >= file_size)
        return -1;
    else if (loc < 12 * BLOCK_SIZE)
        return loc % BLOCK_SIZE;
    else
        return indirect_block[(loc - 12 * BLOCK_SIZE) % BLOCK_SIZE];
}

/**
 * Assign a new block to an i-Node.
 * @param node An i-Node.
 * @return The block idx.
 */
int file_assign_new_block(i_node *node) {
    int vac_block = allocate_a_block_in_bitmap(DATA_BLOCK_START, DATA_BLOCK_START + DATA_BLOCK_LENGTH - 1);
    if (vac_block == -1)
        return -1;

    // Try to assign to the direct pointers.
    for (int i = 0; i < 12; ++i)
        if (node->direct_pointers[i] == -1) {
            node->direct_pointers[i] = vac_block;
            return vac_block;
        }

    // Try to assign to indirect blocks;
    if (node->indirect_pointer == -1)
        node->indirect_pointer = allocate_a_block_in_bitmap(DATA_BLOCK_START, DATA_BLOCK_LENGTH + DATA_BLOCK_START - 1);
    int *buf = malloc(BLOCK_SIZE);
    write_blocks(node->indirect_pointer, 1, buf);
    int i = 0;
    for (; i < BLOCK_SIZE && buf[i] != -1; ++i)
        ;
    buf[i] = vac_block;
    write_blocks(node->indirect_pointer, 1, buf);
    free(buf);
    return vac_block;
}

/**
 * Clear an i-Node.
 * @param i_node_id The i-Node id of the i-Node to clear.
 */
void clear_i_node(int i_node_id) {
    i_node node = i_node_table[i_node_id];
    node.size = -1;
    memset(node.direct_pointers, -1, 12);
    node.indirect_pointer = -1;
    flush_i_node_table();
}

int assign_i_node() {
    int id = get_i_node_first_vacant_spot();
    i_node node = i_node_table[id];
    node.size = 0;
    flush_i_node_table();
    return id;
}

/**
 * Find the first vacant spot in the FDT.
 * @return The idx to the vacant spot.
 */
int get_fdt_first_vacant_spot() {
    for (int i = 0; i < NUM_OF_FILES; ++i)
        if (fdt_table[i].i_node_idx == -1)
            return i;
    return -1;
}

/**
 * @brief Initialize relevant constants given the super_block info.
 *
 * @param superblock The super_block used to initialize constants.
 */
void initialize_constants(super_block *superblock) {
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
        // Initialize according to the super_block.
    }
}

int get_fd_with_i_node_id(int i_node_id){
    for (int i = 0; i < NUM_OF_FILES; ++i)
        if (fdt_table[i].i_node_idx == i_node_id)
            return i;
    return -1;
}

// --------------------------------------------------------------------
// ------------------------------ APIs --------------------------------
// --------------------------------------------------------------------

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
void mksfs(int flag) {}

/**
 * @brief Get the name of the next file.
 *
 * @param filename
 * @return int
 */
int sfs_getnextfilename(char *result_buffer) {
    int count_of_files = count_files_in_root();
    if (count_of_files == 0)
        return -1;
    else if (files_visited == count_of_files - 1) {
        files_visited = 0;
        char *file_name = get_ith_file_in_root(0)->file_name;
        memcpy(result_buffer, file_name, strlen(file_name));
        return 1;
    } else {
        char *file_name = get_ith_file_in_root(files_visited++)->file_name;
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
    directory_entry *result = get_file_in_root(filename);
    return result == NULL ? -1 : i_node_table[result->i_node_id].size;
}

/**
 * @brief Open the specified file. If the file does not exist, create the file.
 *
 * @param filename The specified filename.
 * @return int
 */
int sfs_fopen(char *filename) {
    directory_entry *target = get_file_in_root(filename);
    if (target != NULL) {
        int vac_fdt = get_fdt_first_vacant_spot();
        fdt_table[vac_fdt].i_node_idx = (*target).i_node_id;
        fdt_table[vac_fdt].read_write_pointer = 0;
        return vac_fdt;
    } else {
        int vac_root = get_root_first_vacant_spot(), vac_i_node = get_i_node_first_vacant_spot(), vac_fdt = get_fdt_first_vacant_spot();
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
    if (fdt_table[fd].i_node_idx == -1)
        return -1;
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
            block_id = file_assign_new_block(&node);
            file_size += bytes_to_write;
        } else
            block_id = get_block_id_by_ptr(&node, ptr);

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
        int block_id = get_block_id_by_ptr(&node, ptr);
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
    if (node.size <= loc)
        return -1;
    file.read_write_pointer = loc;
    return 1;
}

/**
 * Remove a file from the file system.
 * @param filename Name of the file to remove.
 * @return 1 for success and -1 otherwise.
 */
int sfs_remove(char *filename) {
    if (get_file_in_root(filename) != NULL)
        return -1;
    directory_entry entry = *get_file_in_root(filename);
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
    for (int i = 0; i < 12 && node.direct_pointers[i] != -1; ++i)
        free_a_block_in_bitmap(node.direct_pointers[i]);

    if (node.indirect_pointer != -1) {
        void *buf = (void *)malloc(BLOCK_SIZE);
        read_blocks(node.indirect_pointer, 1, buf);
        int array_of_pointers[BLOCK_SIZE];
        memcpy(array_of_pointers, buf, BLOCK_SIZE);
        free(buf);
        for (int i = 0; i < BLOCK_SIZE && array_of_pointers[i] != -1; ++i)
            free_a_block_in_bitmap(array_of_pointers[i]);
    }

    flush_i_node_table();
    flush_root_directory_table();

    return 1;
}
