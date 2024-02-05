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
