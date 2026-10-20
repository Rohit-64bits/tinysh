
# tinysh

A lightweight Unix shell written in C++ using POSIX system calls.

`tinysh` is a learning project focused on understanding how command-line shells work internally, including process creation, program execution, pipes, and file descriptor management.

## Features

- **Command execution** using `fork()` and `execvp()`
- **Built-in commands:** `cd` and `pwd`
- **Command argument parsing**, including single and double quotes
- **Input redirection:** `<`
- **Output redirection:** `>`
- **Append redirection:** `>>`
- **Pipelines:** connect multiple commands using `|`
- **Background execution:** run commands using `&`
- **Exit status:** inspect the last command's status using `echo $?`
- **Child-process monitoring** using `waitpid()`

## Requirements

- Linux or a compatible POSIX environment
- A C++ compiler such as `g++`
- Standard POSIX system-call support

## Build

Clone the repository:

```bash
git clone https://github.com/Rohit-64bits/tinysh.git
cd tinysh
```

Compile:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic base.cpp -o tinysh
```

Run:

```bash
./tinysh
```

## Usage

Start the shell and try the following commands:

```bash
pwd
ls -l
cd ..
echo hello
cat file.txt
cat file.txt | grep hello | wc -l
echo hello > output.txt
echo world >> output.txt
cat < output.txt
sleep 5 &
echo $?
exit
```

Replace `file.txt` with a file that exists on your system when testing input redirection.

## Concepts Explored

- Process creation and process IDs
- Parent-child process relationships
- Program replacement with `execvp()`
- Waiting for child processes
- Pipes and file descriptors
- I/O redirection with `open()` and `dup2()`
- Background processes
- Signal handling
- Dynamic command-argument construction with C++ strings and vectors

## Current Limitations

`tinysh` is a learning project and is not intended to be a complete Bash replacement.

- Shell features such as command substitution, environment-variable expansion, and wildcard expansion are not fully implemented.
- Advanced job control and terminal foreground-process-group management are not implemented.
- Parsing and error handling cover a limited subset of shell syntax.

## Learning Goals

The goal is to understand the operating-system interfaces behind an interactive shell and to build up the implementation incrementally.


