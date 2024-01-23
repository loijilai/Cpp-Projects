# Makefile

# Compiler settings
CC = g++
CFLAGS = -Wall -g

# Target executable name
TARGET = main

# Source files
SRC = main.cpp qdbmp.cpp

# Object files
OBJ = $(SRC:.cpp=.o)

# Default target
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

# To obtain object files
%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# To remove generated files
clean:
	rm -f $(OBJ) $(TARGET)
