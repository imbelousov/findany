#include <getopt.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM_NAME "findany"

const struct option long_options[] = {
    {"case-insensitive", no_argument, NULL, 'i'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

#define print_only_usage() printf("Usage: %s [OPTIONS] SUBSTRINGS [FILE]\n", PROGRAM_NAME)

void print_usage()
{
    print_only_usage();
    printf("Try %s --help for more information\n", PROGRAM_NAME);
}

void print_help()
{
    print_only_usage();
    printf("Find any substring from SUBSTRINGS in all lines of FILE and print the ones that contain at least one\n");
    printf("Read standard input if FILE is missing\n");
    printf("\n");
    printf("Options:\n");
    printf("  -i, --case-insensitive    accept the match regardless of upper or lower case\n");
    printf("  -h, --help                print this help\n");
}

#define GETCBUF_BUFFER_SIZE 64 * 1024

int getcbuf(int file)
{
    static unsigned char buffer[GETCBUF_BUFFER_SIZE];
    static size_t buffer_offset = 0;
    static size_t buffer_size = 0;

    if (buffer_offset < buffer_size)
        return buffer[buffer_offset++];

    buffer_size = read(file, buffer, GETCBUF_BUFFER_SIZE);
    buffer_offset = 0;

    if (buffer_offset < buffer_size)
        return buffer[buffer_offset++];
    return EOF;
}

#define READ_LINE_BUFFER_SIZE 120

/**
 * Read the next line from the file
 * @param &buffer Address of the first character of the buffer or NULL.
 * If NULL, allocate memory automatically. Can expand if the next
 * line is longer than buffer_size-1.
 * @param &buffer_size Address of the variable containing size of the buffer
 * @param file Input file handle
 * @return Length of the line including \\n, or 0 if EOF
 */
size_t read_line(char** buffer, size_t* buffer_size, int file)
{
    char* buffer_ = *buffer;
    size_t buffer_size_ = *buffer_size;
    if (buffer_ == NULL)
    {
        buffer_size_ = READ_LINE_BUFFER_SIZE;
        buffer_ = malloc(buffer_size_);
    }
    size_t read = 0;

    int c;
    while ((c = getcbuf(file)) != EOF)
    {
        if (read >= buffer_size_ - 1)
        {
            buffer_size_ *= 2;
            buffer_ = realloc(buffer_, buffer_size_);
            if (buffer_ == NULL)
                exit(EXIT_FAILURE);
        }
        buffer_[read++] = c;
        if (c == '\n')
            break;
    }
    buffer_[read] = '\0';

    *buffer = buffer_;
    *buffer_size = buffer_size_;
    return read;
}

#define BITMAP_WORD_BITS (sizeof(size_t) * 8)

void bitmap_set(size_t* bitmap, size_t idx)
{
    size_t idx_word = idx / BITMAP_WORD_BITS;
    size_t mask = 1 << (idx % BITMAP_WORD_BITS);
    bitmap[idx_word] |= mask;
}

bool bitmap_get(size_t* bitmap, size_t idx)
{
    size_t idx_word = idx / BITMAP_WORD_BITS;
    size_t mask = 1 << (idx % BITMAP_WORD_BITS);
    return bitmap[idx_word] & mask;
}

#define TRIE_DEFAULT_SIZE 64 * 1024
#define TRIE_NODE_SIZE sizeof(struct trie_node)

struct trie_node
{
    /**
     * Index of the next character in list
     */
    ssize_t idx_next;

    /**
    * Index of the first child node
    */
    ssize_t idx_child;

    /**
     * Bit 0 determines whether trie contains the word formed by this and parent nodes or not.
     * Bits 1-255 are set only in the first node in linked list and allow to check if the linked
     * list contains the character or not without full scan.
     */
    size_t bitmap[256 / BITMAP_WORD_BITS];

    /**
     * Stored character or \\0, if node is empty
     */
    char c;
};

struct
{
    struct trie_node* mem;
    size_t size;
    size_t offset;
} trie;

void trie_expand()
{
    trie.size *= 2;
    trie.mem = realloc(trie.mem, trie.size * TRIE_NODE_SIZE);
    if (trie.mem == NULL)
        exit(EXIT_FAILURE);
}

size_t trie_new_node()
{
    if (trie.size <= trie.offset)
        trie_expand();
    struct trie_node new_node;
    new_node.idx_next = -1;
    new_node.idx_child = -1;
    memset(new_node.bitmap, 0, 32);
    new_node.c = '\0';
    trie.mem[trie.offset] = new_node;
    return trie.offset++;
}

void trie_init()
{
    trie.size = TRIE_DEFAULT_SIZE;
    trie.mem = malloc(trie.size * TRIE_NODE_SIZE);
    // Root node
    trie_new_node();
}

void trie_add(ssize_t idx, char* str, size_t length)
{
    while (true)
    {
        char c = *str;
        ssize_t idx_prev;

        bitmap_set(trie.mem[idx].bitmap, (unsigned char)c);

        // Scan linked list inside the node
        do
        {
            if (trie.mem[idx].c == '\0' || trie.mem[idx].c == c)
            {
                trie.mem[idx].c = c;
                break;
            }
            idx_prev = idx;
            idx = trie.mem[idx].idx_next;
        }
        while (idx >= 0);
        
        if (idx < 0)
        {
            // The symbol is not found in the node. Add to the linked list.
            idx = trie_new_node();
            trie.mem[idx_prev].idx_next = idx;
            trie.mem[idx].c = c;
        }
        if (length <= 1)
        {
            bitmap_set(trie.mem[idx].bitmap, 0);
            return;
        }
        if (trie.mem[idx].idx_child < 0)
            trie.mem[idx].idx_child = trie_new_node();

        // Then go to the child node
        idx = trie.mem[idx].idx_child;
        str++;
        length--;
    }
}

void trie_build(char* substrings_filename)
{
    int file = open(substrings_filename, O_RDONLY | O_BINARY);
    if (file < 0)
    {
        printf("No access to file %s", substrings_filename);
        exit(EXIT_FAILURE);
    }

    trie_init();

    char* buffer = NULL;
    size_t buffer_size;
    size_t read;
    while ((read = read_line(&buffer, &buffer_size, file)) > 0)
    {
        if (buffer[read - 1] == '\n')
            buffer[--read] = '\0';
        if (read > 0 && buffer[read - 1] == '\r')
            buffer[--read] = '\0';
        trie_add(0, buffer, read);
    }

    close(file);
    free(buffer);
}

bool trie_find(ssize_t idx, char* str, size_t length)
{
    while (true)
    {
        char c = *str;

        if (!bitmap_get(trie.mem[idx].bitmap, (unsigned char)c))
            return false;

        // Scan linked list inside the node
        do
        {
            if (trie.mem[idx].c == c)
                break;
            idx = trie.mem[idx].idx_next;
        }
        while (idx >= 0);
        if (idx < 0)
            return false;
        if (bitmap_get(trie.mem[idx].bitmap, 0))
            return true;
        if (length <= 1)
            return false;

        // Then go to the child node
        idx = trie.mem[idx].idx_child;
        str++;
        length--;
    }
}

bool trie_find_anywhere(char* str, size_t length)
{
    if (str[length - 1] == '\n')
        length--;
    if (str > 0 && str[length - 1] == '\r')
        length--;
    for (size_t i = 0; i < length; i++)
    {
        if (trie_find(0, str + i, length - i))
            return true;
    }
    return false;
}

void findany(char* substrings_filename, char* input_filename, bool case_insensitive)
{
    trie_build(substrings_filename);

    int src = STDIN_FILENO;
    bool need_close = false;
    if (input_filename != NULL)
    {
        src = open(input_filename, O_RDONLY | O_BINARY);
        if (src < 0)
        {
            printf("No access to file %s", input_filename);
            exit(EXIT_FAILURE);
        }
        need_close = true;
    }
    int dst = STDOUT_FILENO;
    setmode(STDOUT_FILENO, O_BINARY);

    char* buffer = NULL;
    size_t buffer_size;
    size_t read;
    while ((read = read_line(&buffer, &buffer_size, src)) > 0)
    {
        if (trie_find_anywhere(buffer, read))
            write(dst, buffer, read);
    }

    if (need_close)
        close(src);
    free(buffer);
}

int main(int argc, char **argv)
{
    char* substrings_filename;
    char* input_filename = NULL;
    bool case_insensitive = false;

    if (argc <= 1)
    {
        print_usage();
        exit(EXIT_SUCCESS);
    }
    else
    {
        int optc;
        int opti = 1;

        while ((optc = getopt_long(argc, argv, "hi", long_options, NULL)) != -1)
        {
            switch (optc)
            {
            case 'h':
                print_help();
                exit(EXIT_SUCCESS);

            case 'i':
                case_insensitive = true;
                break;

            default:
                print_usage();
                exit(EXIT_FAILURE);
            }
            opti++;
        }

        switch (argc - opti)
        {
        case 2:
            input_filename = argv[opti + 1];
        case 1:
            substrings_filename = argv[opti];
            break;

        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    findany(substrings_filename, input_filename, case_insensitive);
    exit(EXIT_SUCCESS);
}
