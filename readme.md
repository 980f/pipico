A C++ library for generating binary code for the raspberry pi pico state machine

You create objects which represent program components, some configuration, some instructions, and an array of them which is then used to geenerate the config patterns and a program.

This is an alternative to use picoasm and its tools to generate chunks of binary wrapped as C arrays that you #include into your source files.
