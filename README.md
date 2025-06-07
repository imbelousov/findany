# findany

![tests](https://camo.githubusercontent.com/41b829d9a99830f441925cc8fc7f524c0b9a514434cb8c18066b0872f56ac601/68747470733a2f2f696d672e736869656c64732e696f2f62616467652f74657374732d32302532307061737365642d73756363657373)

A command-line utility that retains only those lines from a text file or standard input that contain at least one substring from a list.

## Features

- Search for multiple substrings. It is designed for efficient matching of millions of substrings.
- Reads from standard input or a text file.
- Writes filtered lines to standard output or redirects them to a text file.
- Optional case-insensitive search.
- Optional inversion of search.
- Supports binary files.
- Progress bar to show search progress when processing large files, if the output is redirected to a file.
- Runs on Windows and Linux.

## Build

The program is written in C and can be compiled with `gcc`.
It is recommended to enable SSE4.1 for low-level optimizations.

```
gcc -msse4.1 ./src/findany.c -o findany -O3
```

## Test

Functional tests are configured using YAML files and run with pytest.

```
cd ./test && python -m pytest ./test.py
```

## Usage

```
findany [OPTIONS] [SUBSTRINGS] [FILE]
```

### Options

- `-i, --case-insensitive`: Perform a case-insensitive search. By default, searches are case-sensitive.
- `-v, --invert`: Search for lines that contain none of the specified substrings.
- `-o, --output OUTPUT`: Redirect the output to `OUTPUT` instead of printing to standard output. It enables a progress-bar.
- `-s, --substring SUBSTRING`: Receive a substring from a command-line argument instead of a file. It can be used multiple times. Must not be used together with the SUBSTRINGS argument.
- `-h, --help`: Display the help message and exit.

### Arguments

- `SUBSTRINGS`: A file containing substrings to search for. Each line in this file represents a substring to search for.
- `FILE`: The file to search in. If not provided, standard input will be used.

### Example

1. Search in a file for any of the substrings specified in `substrings.txt` and write the matching lines to `output.txt`:
```
findany -o output.txt substrings.txt input.txt
```

2. Case-insensitive search for substrings:
```
findany -i substrings.txt input.txt
```

3. Read from standard input and write to standard output:
```
cat input.txt | findany substrings.txt > output.txt
```

4. Read from standard input, write to standard output, pass two substrings via command-line arguments:
```
findany -s mySubstring -s otherSubstring < input.txt > output.txt
```

## License

This program is licensed under the GNU General Public License v3.0. See the `LICENSE` file for more details.
