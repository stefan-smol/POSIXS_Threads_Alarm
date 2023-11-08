#!/bin/bash
# Bash script to automate compilation and execution of C program.

# Name of the C program
source_file="new_alarm_mutex"

# Compile the C code and produce an 'a.out' executable
gcc -o "$source_file" "$source_file.c" -D_POSIX_PTHREAD_SEMANTICS -lpthread

# If the compilation was successful, run the 'a.out' executable
if [ $? -eq 0 ]; then
  ./a.out
else
  echo "Compilation failed."
fi