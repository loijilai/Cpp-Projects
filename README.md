# Cpp and C projects
## [Simple jpeg decoder](./my_jpeg/)

## [Bulletin System with I/O multiplexing](./client-server)
A simple server application capable of handling `post`(posting content onto the bullentin board), `pull`(getting content from the bullentin board), `exit`(client exit). The bullentin board can have at most 5 servers accessing and each server can handle up to 20 clients.
* Using `poll` to enable I/O multiplexing in server.c
* Using filelock to prevent race condition

## [Service Management System](./service-management/)
Maintain a service tree consists of processes. Manager get user input from `stdin`, and act as the orchestrator to pass user's request to its children.
* `fork`, `exec` to create service
* `pipe`, `FIFO` to communicate between services
* Note: The `judge.py` is contributed by kenlina

## [Signal-driven User-level Thread Coroutine](./signal-driven-coroutine/)
Implement a signal-driven scheduling mechanism to context switch between threads of execution. The thread here is user-level threads, we did not use `pthread` library here. Use `setjmp`, `longjmp` to context switch between user-level threads mimicking a coroutine behavior.
* Coroutines are computer program components that allow execution to be suspended and resumed, generalizing subroutines for cooperative multitasking.

## [Simple shell: Wish](./wish)
1. This is the project of [OSTEP](https://github.com/remzi-arpacidusseau/ostep-projects/tree/master)
2. See [spec](./wish/README.md)
3. See [document](./wish/DOCUMENTATION.md)