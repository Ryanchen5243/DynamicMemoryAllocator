# Dynamic Memory Allocator

This project is a dynamic memory allocator developed as part of Homework 3 for the CSE 320 course in Spring 2023, taught by Professor Eugene Stark. It demonstrates my understanding of memory allocation concepts and my ability to implement them in C.

## Overview

The dynamic memory allocator is designed for the x86-64 architecture and includes various features to efficiently manage memory. It implements free lists segregated by size class, immediate coalescing of large blocks, boundary tags, block splitting, alignment, and prologue/epilogue techniques. By completing this project, I gained a deeper understanding of dynamic memory allocation, memory padding, alignment, linked lists in C, error handling, and unit testing.

## Key Features

- Free lists segregated by size class, using a first-fit policy within each size class.
- "Quick lists" holding small blocks segregated by size for improved performance.
- Immediate coalescing of large blocks on free with adjacent free blocks.
- Delayed coalescing on free of small blocks.
- Boundary tags for efficient coalescing, with a footer optimization that omits footers from allocated blocks.
- Block splitting without creating splinters.
- Allocated blocks aligned to "single memory row" (8-byte) boundaries.
- Free lists maintained using a Last-In-First-Out (LIFO) discipline.
- Use of a prologue and epilogue to achieve required alignment and avoid edge cases at the end of the heap.

## License

This project is licensed under the [MIT License](LICENSE).

---

**Note**: This README provides an overview of the dynamic memory allocator project. For more detailed information and implementation specifics, please refer to the relevant documentation and source code files.
