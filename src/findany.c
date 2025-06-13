/* 
 * Copyright (c) 2024-2025 Igor Belousov (https://github.com/imbelousov).
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <smmintrin.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#define stat _stat64
#define fstat fstat64
#else /* _WIN32 */
#define O_BINARY 0
#endif /* _WIN32 */

#define PROGRAM_NAME "findany"

const struct option long_options[] = {
    {"case-insensitive", no_argument, NULL, 'i'},
    {"invert", no_argument, NULL, 'v'},
    {"output", required_argument, NULL, 'o'},
    {"substring", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

#define print_only_usage() printf("Usage: %s [OPTIONS] [SUBSTRINGS] [FILE]\n", PROGRAM_NAME)

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
    printf("  -i, --case-insensitive       Perform a case-insensitive search. By default, searches are case-sensitive.\n");
    printf("  -v, --invert                 Search for lines that contain none of the specified substrings.\n");
    printf("  -o, --output OUTPUT          Redirect the output to OUTPUT instead of printing to standard output.\n");
    printf("                               It enables a progress-bar.\n");
    printf("  -s, --substring SUBSTRING    Receive a substring from a command-line argument instead of a file. It can be\n");
    printf("                               used multiple times. Must not be used together with the SUBSTRINGS argument.\n");
    printf("  -h, --help                   Display the help message and exit.\n");
}

#ifdef __SSE4_1__

void* memchr_sse(void* buf, unsigned char val, size_t max_count)
{
    size_t i = 0;
    __m128i vector_val = _mm_set1_epi8(val);
    if (max_count >= 16)
    {
        for (; i <= max_count - 16; i += 16)
        {
            __m128i vector_buf = _mm_loadu_si128(buf + i);
            __m128i vector_result = _mm_cmpeq_epi8(vector_buf, vector_val);
            if (_mm_testz_si128(vector_result, vector_result) == 0)
            {
                for (size_t j = 0; j < 16; j++)
                {
                    if (((unsigned char*)&vector_result)[j] != 0)
                        return buf + i + j;
                }
            }
        }
    }
    for (; i < max_count; i++)
    {
        if (((unsigned char*)buf)[i] == val)
            return buf + i;
    }
    return NULL;
}

int memcmp_sse(void* buf1, void* buf2, size_t size)
{
    size_t i = 0;
    for (; i <= size - 16; i += 16)
    {
        __m128i vector_buf1 = _mm_loadu_si128(buf1 + i);
        __m128i vector_buf2 = _mm_loadu_si128(buf2 + i);
        __m128i cmp = _mm_cmpeq_epi8(vector_buf1, vector_buf2);
        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0xFFFF)
            return 0;
    }
    for (; i < size; i++)
    {
        if (((unsigned char*)buf1)[i] != ((unsigned char*)buf2)[i])
            return 0;
    }
    return 1;
}

#define _memchr(buf, val, max_count) memchr_sse(buf, val, max_count)
#define _memcmp(buf1, buf2, size) memcmp_sse(buf1, buf2, size)

#else /* __SSE4_1__ */

#define _memchr(buf, val, max_count) memchr(buf, val, max_count)
#define _memcmp(buf1, buf2, size) memcmp(buf1, buf2, size)

#endif /* __SSE4_1__ */

#define fatal(...) do\
{\
    printf(__VA_ARGS__);\
    exit(EXIT_FAILURE);\
}\
while (false)
#define fatal_nomem() fatal("Not enough memory")

void* malloc_or_fatal(size_t size)
{
    void* memory = malloc(size);
    if (memory == NULL)
        fatal_nomem();
    return memory;
}

void* realloc_or_fatal(void* memory, size_t new_size)
{
    memory = realloc(memory, new_size);
    if (memory == NULL)
        fatal_nomem();
    return memory;
}

struct string
{
    unsigned char* data;
    size_t length;
};

struct string string_init()
{
    struct string str;
    memset(&str, 0, sizeof(struct string));
    return str;
}

void string_expand(struct string* str, size_t min_length)
{
    if (str->data == NULL)
    {
        str->data = malloc_or_fatal(min_length);
        str->length = min_length;
        return;
    }
    if (str->length >= min_length)
        return;
    str->data = realloc_or_fatal(str->data, min_length);
    str->length = min_length;
}

struct string string_sub(const struct string str, size_t offset, size_t length)
{
    struct string substring;
    substring.data = str.data + offset;
    substring.length = length;
    return substring;
}

void string_to_lower(const struct string src, struct string* dst)
{
    static unsigned char* lookup = NULL;

    if (lookup == NULL)
    {
        // Build a mapping "char -> lowercase char"
        lookup = malloc_or_fatal(256);
        for (int c = 0; c <= 255; c++)
            lookup[c] = tolower(c);
    }

    string_expand(dst, src.length);
    for (size_t i = 0; i < src.length; i++)
        dst->data[i] = lookup[src.data[i]];
}

void string_trim_end(struct string* str, const unsigned char c)
{
    while (str->length > 0 && str->data[str->length - 1] == c)
        str->length--;
}

bool string_starts_with(const struct string str, const struct string substr)
{
    if (str.length < substr.length)
        return false;
    return _memcmp(str.data, substr.data, substr.length) == 0;
}

size_t string_greatest_common_sub(const struct string str1, const struct string str2)
{
    size_t length = str1.length > str2.length ? str2.length : str1.length;
    for (size_t i = 0; i < length; i++)
    {
        if (str1.data[i] != str2.data[i])
            return i;
    }
    return length;
}

void string_destroy(struct string* str)
{
    if (str->data != NULL)
        free(str->data);
    str->data = NULL;
}

#define FSTREAM_BUFFER_DEFAULT_CAPACITY 4 * 1024 * 1024

struct fstream
{
    void* buffer;
    size_t buffer_capacity;
    size_t buffer_size;
    size_t buffer_offset;
    int file;
};

struct fstream fstream_init(int file)
{
    struct fstream stream;
    stream.buffer_capacity = FSTREAM_BUFFER_DEFAULT_CAPACITY;
    stream.buffer = malloc_or_fatal(stream.buffer_capacity);
    stream.buffer_size = 0;
    stream.buffer_offset = 0;
    stream.file = file;
    return stream;
}

void fstream_read_to_buffer(struct fstream* stream)
{
    stream->buffer_size = read(stream->file, stream->buffer, stream->buffer_capacity);
    stream->buffer_offset = 0;
}

struct string fstream_read_line(struct fstream* stream, struct string* buffer, unsigned char delim)
{
    size_t offset = 0;
    if (stream->buffer_offset >= stream->buffer_size)
        fstream_read_to_buffer(stream);
    while (stream->buffer_size > 0)
    {
        void* delimptr = _memchr(stream->buffer + stream->buffer_offset, delim, stream->buffer_size - stream->buffer_offset);
        size_t length = delimptr != NULL
            ? delimptr - stream->buffer - stream->buffer_offset + 1
            : stream->buffer_size - stream->buffer_offset;
        string_expand(buffer, offset + length * 2);
        memcpy(buffer->data + offset, stream->buffer + stream->buffer_offset, length);
        stream->buffer_offset += length;
        offset += length;
        if (delimptr != NULL)
            break;
        if (stream->buffer_offset >= stream->buffer_size)
            fstream_read_to_buffer(stream);
    }
    return string_sub(*buffer, 0, offset);
}

void fstream_destroy(struct fstream* stream)
{
    free(stream->buffer);
    stream->buffer = NULL;
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

#define RADIX_TREE_INITIAL_NODES_CAPACITY 1024
#define RADIX_TREE_INITIAL_KWMEM_CAPACITY 65536

struct radix_tree_node {
    size_t kw_idx;
    size_t kw_length;
    size_t next_node_idx;
    size_t child_node_idx;
    bool leaf;
};

struct {
    struct radix_tree_node* nodes;
    size_t nodes_capacity;
    size_t nodes_length;
    unsigned char* kwmem;
    size_t kwmem_capacity;
    size_t kwmem_length;
} radix_tree;

void radix_tree_init()
{
    radix_tree.nodes = malloc_or_fatal(sizeof(struct radix_tree_node) * RADIX_TREE_INITIAL_NODES_CAPACITY);
    radix_tree.nodes_capacity = RADIX_TREE_INITIAL_NODES_CAPACITY;
    radix_tree.nodes_length = 0;
    radix_tree.kwmem = malloc_or_fatal(RADIX_TREE_INITIAL_KWMEM_CAPACITY);
    radix_tree.kwmem_capacity = RADIX_TREE_INITIAL_KWMEM_CAPACITY;
    radix_tree.kwmem_length = 0;
}

size_t radix_tree_node_add()
{
    if (radix_tree.nodes_length >= radix_tree.nodes_capacity)
    {
        radix_tree.nodes_capacity *= 2;
        radix_tree.nodes = realloc_or_fatal(radix_tree.nodes, sizeof(struct radix_tree_node) * radix_tree.nodes_capacity);
    }
    memset(radix_tree.nodes + radix_tree.nodes_length, 0, sizeof(struct radix_tree_node));
    return radix_tree.nodes_length++;
}

struct radix_tree_node* radix_tree_get_node(size_t node_idx)
{
    return &radix_tree.nodes[node_idx];
}

struct string radix_tree_get_node_kw(size_t node_idx)
{
    struct radix_tree_node* node = radix_tree_get_node(node_idx);
    struct string str = {radix_tree.kwmem + node->kw_idx, node->kw_length};
    return str;
}

size_t radix_tree_kw_add(struct string kw)
{
    if (radix_tree.kwmem_length + kw.length > radix_tree.kwmem_capacity)
    {
        radix_tree.kwmem_capacity *= 2;
        radix_tree.kwmem = realloc_or_fatal(radix_tree.kwmem, radix_tree.kwmem_capacity);
    }
    memcpy(radix_tree.kwmem + radix_tree.kwmem_length, kw.data, kw.length);
    radix_tree.kwmem_length += kw.length;
    return radix_tree.kwmem_length - kw.length;
}

void radix_tree_add(struct string str)
{
    if (radix_tree.nodes_length == 0)
    {
        radix_tree_node_add();
        struct radix_tree_node* root = radix_tree_get_node(0);
        root->kw_idx = radix_tree_kw_add(str);
        root->kw_length = str.length;
        root->leaf = true;
        return;
    }

    size_t node_idx = 0;
    while (true)
    {
        struct radix_tree_node* node = radix_tree_get_node(node_idx);
        struct string node_kw = radix_tree_get_node_kw(node_idx);
        size_t common_sub_length = string_greatest_common_sub(str, node_kw);

        if (common_sub_length == node_kw.length && common_sub_length == str.length)
        {
            // The added string equals the node keyword
            radix_tree_get_node(node_idx)->leaf = true;
            break;
        }
        else if (common_sub_length == 0)
        {
            // The node keyword and the added string have nothing common, go to the next node in the linked list
            if (node->next_node_idx > 0)
                node_idx = node->next_node_idx;
            // Or add a new node to the linked list, if needed
            else
            {
                size_t new_node_idx = radix_tree_node_add();
                node = radix_tree_get_node(node_idx);
                struct radix_tree_node* new_node = radix_tree_get_node(new_node_idx);
                node->next_node_idx = new_node_idx;
                new_node->kw_idx = radix_tree_kw_add(str);
                new_node->kw_length = str.length;
                new_node->leaf = true;
                break;
            }
        }
        else if (common_sub_length == node_kw.length)
        {
            // The node keyword contains a prefix of the added string. Split the string and go to the child node.
            str = string_sub(str, common_sub_length, str.length - common_sub_length);
            if (node->child_node_idx > 0)
                node_idx = node->child_node_idx;
            // Or add a new node as a child node, if needed
            else
            {
                size_t new_node_idx = radix_tree_node_add();
                node = radix_tree_get_node(node_idx);
                struct radix_tree_node* new_node = radix_tree_get_node(new_node_idx);
                node->child_node_idx = new_node_idx;
                new_node->kw_idx = radix_tree_kw_add(str);
                new_node->kw_length = str.length;
                new_node->leaf = true;
                break;
            }
        }
        else if (common_sub_length == str.length)
        {
            // The added string is a prefix of the node keyword. Split the node.
            size_t new_node_idx = radix_tree_node_add();
            node = radix_tree_get_node(node_idx);
            struct radix_tree_node* new_node = radix_tree_get_node(new_node_idx);

            new_node->child_node_idx = node->child_node_idx;
            new_node->leaf = node->leaf;
            new_node->kw_idx = node->kw_idx + common_sub_length;
            new_node->kw_length = node->kw_length - common_sub_length;

            node->child_node_idx = new_node_idx;
            node->leaf = true;
            node->kw_length = common_sub_length;

            break;
        }
        else
        {
            // The added string and the node keyword have the same prefix, but different postfix. Split both the node and the string.
            size_t new_node_idx = radix_tree_node_add();
            node = radix_tree_get_node(node_idx);
            struct radix_tree_node* new_node = radix_tree_get_node(new_node_idx);

            new_node->child_node_idx = node->child_node_idx;
            new_node->leaf = node->leaf;
            new_node->kw_idx = node->kw_idx + common_sub_length;
            new_node->kw_length = node_kw.length - common_sub_length;

            node->child_node_idx = new_node_idx;
            node->leaf = false;
            node->kw_length = common_sub_length;

            str = string_sub(str, common_sub_length, str.length - common_sub_length);
            if (node->next_node_idx > 0)
                node_idx = node->next_node_idx;
            else
            {
                // Add the second child node to the linked list. As result, both new nodes are children of the original one.
                node_idx = new_node_idx;
                new_node_idx = radix_tree_node_add();
                node = radix_tree_get_node(node_idx);
                new_node = radix_tree_get_node(new_node_idx);
                node->next_node_idx = new_node_idx;
                new_node->kw_idx = radix_tree_kw_add(str);
                new_node->kw_length = str.length;
                new_node->leaf = true;
                break;
            }
        }
    }
}

bool radix_tree_find(struct string str)
{
    if (radix_tree.nodes_length == 0)
        return false;

    size_t idx = 0;
    do
    {
        struct radix_tree_node node = radix_tree.nodes[idx];
        struct string node_kw = {radix_tree.kwmem + node.kw_idx, node.kw_length};

        if (node.kw_length > str.length)
        {
            if (string_starts_with(node_kw, str))
                return false;
            else
                idx = node.next_node_idx;
        }
        else if (string_starts_with(str, node_kw))
        {
            if (node.leaf)
                return true;
            str = string_sub(str, node_kw.length, str.length - node_kw.length);
            idx = node.child_node_idx;
        }
        else
            idx = node.next_node_idx;
    } while(idx > 0 && str.length > 0);

    return false;
}

void radix_tree_build_from_file(unsigned char* substrings_filename, bool case_insensitive)
{
    int file = open(substrings_filename, O_RDONLY | O_BINARY);
    if (file < 0)
        fatal("No access to file %s", substrings_filename);

    radix_tree_init();

    struct fstream stream = fstream_init(file);
    struct string buffer = string_init();
    while (true)
    {
        struct string substring = fstream_read_line(&stream, &buffer, '\n');
        if (substring.length == 0)
            break;
        if (case_insensitive)
            string_to_lower(substring, &substring);

        if (substring.data[substring.length - 1] == '\n')
            substring = string_sub(substring, 0, substring.length - 1);
        if (substring.length > 0 && substring.data[substring.length - 1] == '\r')
            substring = string_sub(substring, 0, substring.length - 1);
        if (substring.length == 0)
            break;

        radix_tree_add(substring);
    }
}

void radix_tree_build_from_args(struct string* substrings, size_t substrings_count, bool case_insensitive)
{
    radix_tree_init();

    for (size_t i = 0; i < substrings_count; i++)
    {
        struct string substring = substrings[i];
        if (substring.length == 0)
            continue;
        if (case_insensitive)
            string_to_lower(substring, &substring);

        radix_tree_add(substring);
    }
}

bool radix_tree_find_anywhere(struct string str)
{
    string_trim_end(&str, '\n');
    string_trim_end(&str, '\r');
    for (size_t i = 0; i < str.length; i++)
    {
        if (radix_tree_find(string_sub(str, i, str.length - i)))
            return true;
    }
    return false;
}

void format_size(size_t size, char* buffer)
{
    if ((size >> 11) == 0)
        sprintf(buffer, "%zu", size);
    else if ((size >> 21) == 0)
    {
        float k = (float) size / 1024.0f;
        sprintf(buffer, "%.2fK", k);
    }
    else if ((size >> 31) == 0)
    {
        float m = (float) size / (1024.0f * 1024.0f);
        sprintf(buffer, "%.2fM", m);
    }
    else
    {
        float g = (float) size / (1024.0f * 1024.0f * 1024.0f);
        sprintf(buffer, "%.2fG", g);
    }
}

char* build_progress_str(size_t processed, size_t size)
{
    static char buffer[1024];

    char processed_str[256];
    char size_str[256];
    format_size(processed, processed_str);
    format_size(size, size_str);
    
    // Progress bar
    char progress_str[256] = "";
    if (size > 0)
    {
        const int progress_bar_len = 32;
        char progress_bar[progress_bar_len + 1];
        float progress = (float) processed / (float) size;
        memset(progress_bar, ' ', progress_bar_len);
        progress_bar[0] = progress_bar[progress_bar_len - 1] = '|';
        progress_bar[progress_bar_len] = '\0';
        for (int i = 0; i < (progress_bar_len - 2) * progress; i++)
            progress_bar[i + 1] = '*';
        sprintf(progress_str, "%s %.2f%%   ", progress_bar, progress * 100.0f);
    }

    sprintf(buffer, "%s%s / %s", progress_str, processed_str, size > 0 ? size_str : "?");

    return buffer;
}

void print_ws(size_t length)
{
    char buffer[256];
    memset(buffer, ' ', length);
    buffer[length] = '\0';
    printf("%s", buffer);
}

void print_progress(size_t processed, size_t size, bool force)
{
    static clock_t prevtime = 0;
    static size_t prevlength = 0;
    clock_t time = clock();
    if (prevtime == 0)
    {
        prevtime = time;
        return;
    }
    if (time - prevtime > CLOCKS_PER_SEC || force)
    {
        if (processed > size)
            size = processed;

        char* progress_str = build_progress_str(processed, size);
        size_t length = strlen(progress_str);
        printf("\r%s", progress_str);
        fflush(stdout);
        if (prevlength > length)
            print_ws(prevlength - length);
        prevtime = time;
        prevlength = length;
        prevtime = time;
    }
}

void handle_line(struct string line_for_search, struct string line_original, size_t input_size, int output_file, unsigned char* output_filename, bool invert, size_t* progress)
{
    if (radix_tree_find_anywhere(line_for_search) ^ invert)
    {
        if (write(output_file, line_original.data, line_original.length) < 0)
            fatal("Failed to write");
    }
    *progress += line_original.length;
    if (output_filename != NULL)
        print_progress(*progress, input_size, false);
}

void findany(unsigned char* substrings_filename, struct string* substrings, size_t substrings_count, unsigned char* input_filename, unsigned char* output_filename, bool case_insensitive, bool invert)
{
    if (substrings_filename != NULL)
        radix_tree_build_from_file(substrings_filename, case_insensitive);
    else
        radix_tree_build_from_args(substrings, substrings_count, case_insensitive);

    // Initialize input
    int input_file = STDIN_FILENO;
    bool input_need_close = false;
    size_t input_size = 0;
    if (input_filename != NULL)
    {
        input_file = open(input_filename, O_RDONLY | O_BINARY);
        if (input_file < 0)
            fatal("No access to file %s", input_filename);
        input_need_close = true;
        struct stat stat;
        if (fstat(input_file, &stat) >= 0)
            input_size = stat.st_size;
    }
#ifdef _WIN32
    else
        setmode(input_file, O_BINARY);
#endif /* _WIN32 */

    // Initialize output
    int output_file = STDOUT_FILENO;
    bool output_need_close = false;
    if (output_filename != NULL)
    {
        output_file = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
        if (output_file < 0)
            fatal("No access to file %s", output_filename);
        output_need_close = true;
    }
#ifdef _WIN32
    else
        setmode(output_file, O_BINARY);
#endif /* _WIN32 */

    struct fstream input_stream = fstream_init(input_file);
    struct string buffer = string_init();
    size_t progress = 0;

    if (case_insensitive)
    {
        struct string lower_buffer = string_init();
        while (true)
        {
            struct string line = fstream_read_line(&input_stream, &buffer, '\n');
            if (line.length == 0)
                break;
            string_to_lower(line, &lower_buffer);
            handle_line(string_sub(lower_buffer, 0, line.length), line, input_size, output_file, output_filename, invert, &progress);
        }
        string_destroy(&lower_buffer);
    }
    else
    {
        while (true)
        {
            struct string line = fstream_read_line(&input_stream, &buffer, '\n');
            if (line.length == 0)
                break;
            handle_line(line, line, input_size, output_file, output_filename, invert, &progress);
        }
    }
    if (output_filename != NULL)
    {
        print_progress(progress, input_size, true);
        printf("\n");
    }

    string_destroy(&buffer);
    fstream_destroy(&input_stream);
    if (input_need_close)
        close(input_file);
    if (output_need_close)
        close(output_file);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    unsigned char* substrings_filename = NULL;
    struct string* substrings = NULL;
    size_t substrings_count = 0;
    unsigned char* input_filename = NULL;
    unsigned char* output_filename = NULL;
    bool case_insensitive = false;
    bool invert = false;

    if (argc <= 1)
    {
        print_usage();
        exit(EXIT_SUCCESS);
    }
    else
    {
        int optc;
        while ((optc = getopt_long(argc, argv, "hivo:s:", long_options, NULL)) != -1)
        {
            switch (optc)
            {
            case 'h':
                print_help();
                exit(EXIT_SUCCESS);

            case 'i':
                case_insensitive = true;
                break;

            case 'v':
                invert = true;
                break;

            case 'o':
                output_filename = optarg;
                break;

            case 's':
                substrings = realloc_or_fatal(substrings, sizeof(struct string) * (substrings_count + 1));
                struct string substring = {optarg, strlen(optarg)};
                substrings[substrings_count++] = substring;
                break;

            default:
                print_usage();
                exit(EXIT_FAILURE);
            }
        }

        switch (argc - optind)
        {
        case 2:
            input_filename = argv[optind + 1];
        case 1:
            if (substrings != NULL)
                input_filename = argv[optind];
            else
                substrings_filename = argv[optind];
            break;
        case 0:
            if (substrings != NULL)
                break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }

        if (substrings != NULL && substrings_filename != NULL)
        {
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    findany(substrings_filename, substrings, substrings_count, input_filename, output_filename, case_insensitive, invert);
    exit(EXIT_SUCCESS);
}
