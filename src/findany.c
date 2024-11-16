#include <stdio.h>
#include <getopt.h>

#define PROGRAM_NAME "findany"

const struct option long_options[] = {
    {"case-insensitive", no_argument, NULL, 'i'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

#define print_only_usage() printf("Usage: %s [OPTIONS] SUBSTRINGS [FILE]\n", PROGRAM_NAME)

void print_usage()
{
    print_only_usage();
    printf("Try %s --help for more information", PROGRAM_NAME);
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

int case_insensitive = 0;
char* substrings_filename;
char* output_filename = NULL;

int main(int argc, char **argv)
{
    if (argc <= 1)
        print_usage();
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
                return 0;

            case 'i':
                case_insensitive = 1;
                break;

            default:
                print_usage();
                return 1;
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
            return 1;
        }
    }
    return 0;
}
