#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM_NAME "findany"

int case_insensitive = 0;
char* substrings_filename;
char* input_filename = NULL;

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

#define READ_LINE_BUFFER_SIZE 120
#define READ_LINE_BUFFER_MARK 0x7F

/**
 * Read the next line from the file
 * @param &buffer Address of the first character of the buffer or NULL.
 * If NULL, allocate memory automatically. Can expand if the next
 * line is longer than buffer_size-1.
 * @param &buffer_size Address of the variable containing size of the buffer
 * @param fp Input file handle
 * @return Length of the line including \\n, or 0 if EOF
 */
size_t read_line(char** buffer, size_t* buffer_size, FILE* fp)
{
    char* buffer_ = *buffer;
    size_t buffer_size_ = *buffer_size;

    if (buffer_ == NULL)
    {
        buffer_size_ = READ_LINE_BUFFER_SIZE;
        buffer_ = malloc(buffer_size_);
    }
    buffer_[buffer_size_ - 1] = READ_LINE_BUFFER_MARK;

    size_t offset = 0;
    while (true)
    {
        bool eof = fgets(buffer_ + offset, buffer_size_ - offset, fp) == NULL;
        if (eof)
        {
            if (!feof(fp))
                exit(EXIT_FAILURE);
            else
                break;
        }

        // Optimization which allows to skip full scan of the buffer in order to determine length of the line
        if (buffer_[buffer_size_ - 1] == READ_LINE_BUFFER_MARK || buffer_[buffer_size_ - 2] == '\n')
        {
            offset += strlen(buffer_ + offset);
            break;
        }

        offset = buffer_size_ - 1;
        buffer_size_ *= 2;
        buffer_ = realloc(buffer_, buffer_size_);
        if (buffer_ == NULL)
            exit(EXIT_FAILURE);
        buffer_[buffer_size_ - 1] = READ_LINE_BUFFER_MARK;
    }

    *buffer = buffer_;
    *buffer_size = buffer_size_;
    return offset;
}

#define TRIE_DEFAULT_SIZE 64 * 1024
#define TRIE_NODE_SIZE sizeof(struct trie_node)
#define TRIE_EMPTY_NODE {-1, -1, '\0', false}

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
     * Stored character or \\0, if node is empty
     */
    char c;

    /**
     * Determines whether trie contains the word formed by this and parent nodes or not
     */
    bool is_leaf;
};

struct trie_node* trie;
size_t trie_size;
size_t trie_offset;

void trie_init()
{
    trie_size = TRIE_DEFAULT_SIZE;
    trie = malloc(trie_size * TRIE_NODE_SIZE);
    struct trie_node root = TRIE_EMPTY_NODE;
    trie[0] = root;
    trie_offset = 1;
}

void trie_expand()
{
    trie_size *= 2;
    trie = realloc(trie, trie_size * TRIE_NODE_SIZE);
    if (trie == NULL)
        exit(EXIT_FAILURE);
}

size_t trie_new_node()
{
    if (trie_size <= trie_offset)
        trie_expand();
    struct trie_node new_node = TRIE_EMPTY_NODE;
    trie[trie_offset] = new_node;
    return trie_offset++;
}

void trie_add(ssize_t idx, char* str, size_t length)
{
    char c = *str;
    ssize_t idx_prev;
    do
    {
        if (trie[idx].c == '\0' || trie[idx].c == c)
        {
            trie[idx].c = c;
            break;
        }
        idx_prev = idx;
        idx = trie[idx].idx_next;
    }
    while (idx >= 0);
    if (idx < 0)
    {
        idx = trie_new_node();
        trie[idx_prev].idx_next = idx;
        trie[idx].c = c;
    }
    if (length <= 1)
    {
        trie[idx].is_leaf = true;
        return;
    }
    if (trie[idx].idx_child < 0)
        trie[idx].idx_child = trie_new_node();
    trie_add(trie[idx].idx_child, str + 1, length - 1);
}

void trie_build()
{
    FILE* fp = fopen(substrings_filename, "r");
    if (fp == NULL)
    {
        printf("No access to file %s", substrings_filename);
        exit(EXIT_FAILURE);
    }

    trie_init();

    char* buffer = NULL;
    size_t buffer_size;
    size_t read;
    while ((read = read_line(&buffer, &buffer_size, fp)) > 0)
    {
        if (buffer[read - 1] == '\n')
            buffer[--read] = '\0';
        if (read > 0 && buffer[read - 1] == '\r')
            buffer[--read] = '\0';
        trie_add(0, buffer, read);
    }

    fclose(fp);
    free(buffer);
}

bool trie_find(ssize_t idx, char* str, size_t length)
{
    char c = *str;
    do
    {
        if (trie[idx].c == c)
            break;
        idx = trie[idx].idx_next;
    }
    while (idx >= 0);
    if (idx < 0)
        return false;
    if (trie[idx].is_leaf)
        return true;
    if (length <= 1)
        return false;
    return trie_find(trie[idx].idx_child, str + 1, length - 1);
}

bool filter_input(char* str, size_t length)
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

void scan_input()
{
    FILE* fp = stdin;
    bool need_close = false;
    if (input_filename != NULL)
    {
        fp = fopen(input_filename, "r");
        if (fp == NULL)
        {
            printf("No access to file %s", input_filename);
            exit(EXIT_FAILURE);
        }
        need_close = true;
    }

    char* buffer = NULL;
    size_t buffer_size;
    size_t read;
    while ((read = read_line(&buffer, &buffer_size, fp)) > 0)
    {
        if (filter_input(buffer, read))
            fwrite(buffer, 1, read, stdout);
    }

    if (need_close)
        fclose(fp);
    free(buffer);
}

int main(int argc, char **argv)
{
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
                case_insensitive = 1;
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

    trie_build();
    scan_input();
    exit(EXIT_SUCCESS);
}
