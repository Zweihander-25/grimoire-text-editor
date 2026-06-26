# Grimoire Text Editor

Grimoire is a terminal-based text editor written in C. It is a personal learning project focused on understanding how text editors work internally by building one from scratch.

The project is currently in its early stages and is based on the Kilo text editor tutorial. As development progresses, the goal is to evolve it into an independent editor with additional features.

## Current Features

* Terminal raw mode
* Basic screen rendering
* Display the contents of a text file in the terminal (text viewer)

## Project Structure

```
.
├── grimoire.c
├── Makefile
└── README.md
```

## Build

```bash
make
```

## Run

```bash
./grimoire <filename>
```

Example:

```bash
./grimoire test.txt
```

## Roadmap

* [x] Display file contents
* [x] Cursor movement
* [x] Scrolling
* [ ] Text editing
* [ ] Saving files
* [ ] Search
* [ ] Syntax highlighting
* [ ] Undo/Redo

## Learning Goals

This project is helping me learn:

* Systems programming in C
* Terminal programming
* ANSI escape sequences
* File I/O
* Memory management
* Data structures

## Acknowledgements

This project is inspired by the **Build Your Own Text Editor** tutorial by Salvatore Sanfilippo (antirez):

https://viewsourcecode.org/snaptoken/kilo/

The tutorial serves as the starting point for learning. Future development will focus on making Grimoire an independent project with original features.
