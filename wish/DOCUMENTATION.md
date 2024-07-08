# Implementation Details

## Function
### Internal commands: `cd`, `path`, `exit`
#### 1. `cd`
#### 2. `path`
1. I will free the space pointed by path everytime, even if the space pointed by path is larger than needed in order to simplify logic (maintaining the size of path)
2. Each token is copied from `myargv` into `*p_path` by `strdup` because I will free the whole buffer `myargv` in each iteration (I handle user's input one at a time in each iteration). If I did not make a copy, the pointer to tokens will be meaningless after I free `myargv`
#### 3. `exit`

### External commands
The shell will search path first to find the executable, redirect, and call `execv`
#### 1. `get_fd`
1. I use two pointers left and right. Whenever right find redirect symbol, it assign the redirect symbol to NULL and advances to the next position, and open the file for reading/writing (depends on < or >). This behavior enables the behavior of `ls > output1.out > output2.out`, where output1.out will be open and truncated to clear all content, but all content will be written into output2.out (Because output1.out is open before, but only the file behind the last redirection symbol will be written into).

2. When user types `ls > output1.out output2.out output3.out -la`, the result will be redirected to output1.out, where two files, output2.out and output3.out, will have their properties listed as specified as -la. This is because ls can accept filename as argument. When I passed this command into `get_fd`, the result is `ls output2.out output3.out` with fd_out set to output1.out. By passing output2.out and output3.out as arguments to ls, I correctly handle this case.
## Dynamic memory management
### `parse_args`
1. `strsep` will modify the pointer, but the `command` passed in is a formal variable so it can be modified.
2. I use the dynamic array to handle the number of arguments parsed
3. I always keep the last element of argv equals NULL by resizing when `if(used >= size-1)`
4. User should free argv after `parse_args` returns
```
/**
 * Parses a command line input into an array of command/arguments.
 *
 * This function takes a string containing a command followed by its arguments,
 * separated by spaces, and parses it into an array of strings where the first element
 * is the command and the subsequent elements are the arguments. The function handles
 * dynamic memory allocation for the array and each argument. The caller is responsible
 * for freeing the allocated memory.
 *
 * @param input The input command line string to parse.
 * @param argv A pointer to an array of strings that will hold the command and its arguments.
 * @param argc A pointer to an integer that will store the number of arguments (including the command).
 * @return Returns 0 on successful parsing, -1 on failure due to memory allocation issues.
 *
 * Example usage:
 * char **argv;
 * int argc;
 * if (parse_command("ls -l /home", &argv, &argc) == 0) {
 *     for (int i = 0; i < argc; i++) {
 *         printf("Argument %d: %s\n", i, argv[i]);
 *         free(argv[i]);  // Free each argument
 *     }
 *     free(argv);  // Free the argument array
 * }
 */

```

### What if `execv` fail?
1. Q: If I fork a process and the child process fail to execute execv and return. Should I call exit after the child process return from execv?
A: Yes, this prevents the child from continuing to execute the same code as the parent, which can lead to unexpected behavior or errors in your application.  
Note: It's preferable to use `_exit()` because _exit() or _Exit() exits the process immediately without calling cleanup functions registered with atexit() or flushing stdio buffers. This is often preferable from within a child process created by fork() before a successful execv, because it prevents duplicate side effects from library cleanups in the child.

2. Q: If I malloc a space before fork, and the execv call fail. Do I need to free the space both child process and parent process?
A: Because child will `_exit()` soon so it's not necessary, but it's a good practice if the child process keep running for some cleanup job. If execv sucess, the address space of the child process will be overwrite, so the free is done implicitly. The parent should free just as the ordinary program.

## Potential errors
### Redirection
1. I need a more robust argument parsing to handle this case: `ls tests/p2a-test>/tmp/output11` (redirection symbol without spaces around)