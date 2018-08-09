FROM ubuntu AS omr-brainfuck-build

RUN apt-get update && apt-get install -y \
 build-essential \
 cmake \
 libdwarf-dev \
 libelf-dev \
 ninja-build \
 git \
 nano \
 golang-go \
 vim

WORKDIR /
COPY . /omr-brainfuck

WORKDIR /omr-brainfuck
RUN mkdir build

WORKDIR /omr-brainfuck/build
RUN cmake -G Ninja .. 
RUN ninja


FROM ubuntu AS omr-brainfuck
EXPOSE 50055
COPY --from=omr-brainfuck-build /omr-brainfuck/build /omr-brainfuck/build
ENTRYPOINT ["/omr-brainfuck/build/bf"]
