#include <getopt.h>
#include <fcntl.h>
#include <stdbool.h>
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

struct
{
    struct trie_node* mem;
    size_t size;
    size_t offset;
    bool rootchars[256];
} trie;

void trie_init()
{
    trie.size = TRIE_DEFAULT_SIZE;
    trie.mem = malloc(trie.size * TRIE_NODE_SIZE);
    struct trie_node root = TRIE_EMPTY_NODE;
    trie.mem[0] = root;
    trie.offset = 1;
    memset(trie.rootchars, false, 256);
}

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
    struct trie_node new_node = TRIE_EMPTY_NODE;
    trie.mem[trie.offset] = new_node;
    return trie.offset++;
}

void trie_add(ssize_t idx, char* str, size_t length)
{
    trie.rootchars[*str] = true;
    while (true)
    {
        char c = *str;
        ssize_t idx_prev;

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
            trie.mem[idx].is_leaf = true;
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
        if (trie.mem[idx].is_leaf)
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
        if (!trie.rootchars[*(str + i)])
            continue;
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
