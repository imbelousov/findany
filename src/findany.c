#include <getopt.h>
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

#define READ_LINE_BUFFER_SIZE 4
#define READ_LINE_BUFFER_MARK 0x7F

/*
 * Read the next line from the file
 * @param &buffer Address of the first character of the buffer or NULL.
 * If NULL, allocate memory automatically. Can expand if the next
 * line is longer than bufferSize-1.
 * @param &bufferSize Address of the variable containing size of the buffer
 * @param fp Input file handle
 * @return Length of the line including \\n, or 0 if EOF
 */
ssize_t read_line(char** buffer, size_t* bufferSize, FILE* fp)
{
    char* buffer_ = *buffer;
    size_t bufferSize_ = *bufferSize;

    if (buffer_ == NULL)
    {
        bufferSize_ = READ_LINE_BUFFER_SIZE;
        buffer_ = malloc(bufferSize_);
    }
    buffer_[bufferSize_ - 1] = READ_LINE_BUFFER_MARK;

    size_t offset = 0;
    while (1)
    {
        int eof = fgets(buffer_ + offset, bufferSize_ - offset, fp) == NULL;
        if (eof)
        {
            if (!feof(fp))
                exit(EXIT_FAILURE);
            else
                break;
        }

        // Optimization which allows to skip full scan of the buffer in order to determine length of the line
        if (buffer_[bufferSize_ - 1] == READ_LINE_BUFFER_MARK || buffer_[bufferSize_ - 2] == '\n')
        {
            offset += strlen(buffer_ + offset);
            break;
        }

        offset = bufferSize_ - 1;
        bufferSize_ *= 2;
        buffer_ = realloc(*buffer, bufferSize_);
        if (buffer_ == NULL)
            exit(EXIT_FAILURE);
        buffer_[bufferSize_ - 1] = READ_LINE_BUFFER_MARK;
    }

    *buffer = buffer_;
    *bufferSize = bufferSize_;
    return offset;
}

int case_insensitive = 0;
char* substrings_filename;
char* output_filename = NULL;

void build_trie()
{
    printf("Read file %s\n", substrings_filename);

    FILE* fp = fopen(substrings_filename, "r");
    if (fp == NULL)
    {
        printf("No access to file %s", substrings_filename);
        exit(EXIT_FAILURE);
    }

    char* buffer = NULL;
    size_t bufferSize;
    ssize_t read;
    while ((read = read_line(&buffer, &bufferSize, fp)) > 0)
    {

    }

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
        case 1:
            substrings_filename = argv[opti];

        case 2:
            output_filename = argv[opti + 1];
            break;

        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    build_trie();

    exit(EXIT_SUCCESS);
}
