# omr-brainfuck

Simple brainfuck interpreter using Eclipse OMR's JitBuilder framework.  The
brainfuck programs are compiled to machine code before running.

## Compiling

Clone the repo and submodules, and then make!  It requires boost, although that
will probably change in the future.

```sh
git clone --recursive https://github.com/youngar/omr-brainfuck.git
make
```

## Usage

The `bf` executable takes one argument, the name of the brainfuck file to run. There are some examplethe `bf` executable takes one argument, the name of the brainfuck file to run. 

```
./bf ./brainfuck/programs/hello.bf
OMR BrainFuck Interpreter
done compiling
Hello World!
```

```
0
1
1
2
3
5
8
13
21
34
55
89
144
233
377
610
987
1597
2584
4181
6765
10946
17711
28657
46368
75025
```
