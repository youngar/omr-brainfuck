#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <stack>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Jit.hpp"
#include "ilgen/JitBuilderRecorder.hpp"
#include "ilgen/JitBuilderRecorderTextFile.hpp"
#include "ilgen/MethodBuilder.hpp"
#include "ilgen/TypeDictionary.hpp"
#include "imperium/imperium.hpp"

namespace bf {

using TapeCell = uint8_t;

extern "C" {

TapeCell bf_get_character() {
  TapeCell input;
  std::cin >> input;
  return input;
}

void bf_put_character(TapeCell character) { std::cout << character; }

} // extern "C"

class MethodBuilder final : public TR::MethodBuilder {
public:
  explicit MethodBuilder(TR::TypeDictionary *types, char *byteCodes,
                         std::size_t numberOfByteCodes, TapeCell *tape,
                         TR::JitBuilderRecorder *recorder = nullptr);

private:
  bool buildIL() override;

  // For local caching
  TR::IlValue *getLocal(TR::IlBuilder *b, std::map<int, TR::IlValue *> &locals,
                        int local);
  void setLocal(TR::IlBuilder *b, std::map<int, TR::IlValue *> &locals,
                int local, TR::IlValue *value);
  void commitLocalsToTape(TR::IlBuilder *b,
                          std::map<int, TR::IlValue *> &locals);

  TR::IlType *tapeCellType;
  TR::IlType *tapeCellPointerType;

  char *byteCodes_ = nullptr;
  std::size_t numberOfByteCodes_ = 0;
  std::string server_ = "";
  TapeCell *tape_ = nullptr;
};

class BrainFuckVM final {
public:
  void runByteCodes(char *byteCodes, std::size_t numberOfByteCodes);
  void setServer(std::string server) { server_ = server; }

  static const std::size_t tapeSize = 30000;

private:
  std::string server_ = "";

  /* brainfuck mandates the minimum tape size */
  TapeCell tape_[tapeSize];
};

MethodBuilder::MethodBuilder(TR::TypeDictionary *types, char *byteCodes,
                             std::size_t numberOfByteCodes, TapeCell *tape,
                             TR::JitBuilderRecorder *recorder)
    : TR::MethodBuilder(types, recorder), tapeCellType(Int8),
      tapeCellPointerType(types->PointerTo(Int8)), byteCodes_(byteCodes),
      numberOfByteCodes_(numberOfByteCodes), tape_(tape) {

  DefineLine(LINETOSTR(__LINE__));
  DefineFile(__FILE__);
  DefineName("brainfuck");

  /* tell the compiler that compiled programs do no return anything */
  DefineReturnType(Int32);
  // DefineReturnType(NoType);
  DefineParameter("tapeCellPointer", tapeCellPointerType);
  DefineFunction("putCharacter", __FILE__, "putCharacter",
                 reinterpret_cast<void *>(&bf_put_character), NoType, 1,
                 tapeCellType);
  DefineFunction("getCharacter", __FILE__, "getCharacter",
                 reinterpret_cast<void *>(&bf_get_character), tapeCellType, 0);
  AllLocalsHaveBeenDefined();
}

void BrainFuckVM::runByteCodes(char *byteCodes, std::size_t numberOfByteCodes) {

  uint8_t *entryPoint;

  // Zero the tape
  std::memset(tape_, 0, BrainFuckVM::tapeSize);

  if (server_.empty()) {
    TR::TypeDictionary types;
    MethodBuilder mb(&types, byteCodes, numberOfByteCodes, tape_);

    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

    int rc = compileMethodBuilder(&mb, &entryPoint);
    if (rc != 0) {
      std::cout << "Compilation failed" << std::endl;
      return;
    }
    std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>( t2 - t1 ).count();
    std::cout << "Duration of compilation on client: " << duration << " microseconds." << '\n';

    void (*compiledFunction)(TapeCell *) =
        reinterpret_cast<decltype(compiledFunction)>(entryPoint);
    (*compiledFunction)(tape_);
  } else {
    char *tmpFile = std::tmpnam(nullptr);

    TR::TypeDictionary types;
    TR::JitBuilderRecorderTextFile recorder(nullptr, tmpFile);
    MethodBuilder mb(&types, byteCodes, numberOfByteCodes, tape_, &recorder);

    OMR::Imperium::ClientChannel client(server_);

    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

    client.requestCompileSync(tmpFile, &entryPoint, &mb);

    std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>( t2 - t1 ).count();
    std::cout << "Duration of compilation on server: " << duration << " microseconds." << '\n';

    void (*compiledFunction)(TapeCell *) =
        reinterpret_cast<decltype(compiledFunction)>(entryPoint);
    (*compiledFunction)(tape_);
    client.shutdown();
  }
}

/* Cache local tape values */
TR::IlValue *MethodBuilder::getLocal(TR::IlBuilder *b,
                                     std::map<int, TR::IlValue *> &locals,
                                     int local) {
  TR::IlValue *value = nullptr;
  auto search = locals.find(local);
  if (search == locals.end()) {
    value = b->LoadAt(tapeCellPointerType,
                      b->Add(b->Load("tapeCellPointer"), b->ConstInt64(local)));
    locals[local] = value;
  } else {
    value = search->second;
  }

  return value;
}

void MethodBuilder::setLocal(TR::IlBuilder *b,
                             std::map<int, TR::IlValue *> &locals, int local,
                             TR::IlValue *value) {
  locals[local] = value;
}

void MethodBuilder::commitLocalsToTape(TR::IlBuilder *b,
                                       std::map<int, TR::IlValue *> &locals) {
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    TR::IlValue *pointerLocation =
        b->Add(b->Load("tapeCellPointer"), b->ConstInt64(it->first));
    b->StoreAt(pointerLocation, it->second);
  }

  locals.clear();
}

bool MethodBuilder::buildIL() {
  std::stack<TR::IlBuilder *> openStack;
  std::stack<TR::IlBuilder *> closeStack;
  std::map<int, TR::IlValue *> locals;
  TR::IlBuilder *openBuilder;
  TR::IlBuilder *closeBuilder;
  TR::IlBuilder *b = this;

  TR::IlValue *pointerLocation = nullptr;
  // b->Store("tapeCellPointer", b->ConstAddress(tape_));
  int64_t tapeCellPointerOffset = 0;

  for (const char *byteCode = byteCodes_;
       byteCode < &byteCodes_[numberOfByteCodes_]; byteCode++) {
    switch (*byteCode) {
    case '>': // Move the pointer forward by 1.
      tapeCellPointerOffset++;
      break;
    case '<': // Move the pointer backward by 1.
      tapeCellPointerOffset--;
      break;
    case '+': // Increment the cell at the current pointer.
      setLocal(
          b, locals, tapeCellPointerOffset,
          b->Add(getLocal(b, locals, tapeCellPointerOffset), b->ConstInt8(1)));
      break;
    case '-': // Decrements the cell at the current pointer.
      setLocal(
          b, locals, tapeCellPointerOffset,
          b->Sub(getLocal(b, locals, tapeCellPointerOffset), b->ConstInt8(1)));
      break;
    case '.': // Output the character at the current cell.
      b->Call("putCharacter", 1, getLocal(b, locals, tapeCellPointerOffset));
      break;
    case ',': // Read a character into the current cell (for our purposes, EOF
              // produces 0).
      setLocal(b, locals, tapeCellPointerOffset, b->Call("getCharacter", 0));
      break;
    case '[': // If the current cell is zero, skips to the matching ] later on.
      openBuilder = OrphanBuilder();
      closeBuilder = OrphanBuilder();

      commitLocalsToTape(b, locals);

      // reset the pointer offset when entering a new block
      pointerLocation = b->Add(b->Load("tapeCellPointer"),
                               b->ConstInt64(tapeCellPointerOffset));
      b->Store("tapeCellPointer", pointerLocation);
      tapeCellPointerOffset = 0;

      b->IfThenElse(&openBuilder, &closeBuilder,
                    b->NotEqualTo(b->ConstInt8(0),
                                  b->LoadAt(tapeCellPointerType,
                                            b->Load("tapeCellPointer"))));
      b = openBuilder;

      openStack.push(openBuilder);
      closeStack.push(closeBuilder);
      break;
    case ']': // If the current cell is non-zero, skips to the matching [
              // earlier on.
      closeBuilder = closeStack.top();
      openBuilder = openStack.top();

      commitLocalsToTape(b, locals);

      // reset the pointer offset when entering a new block
      pointerLocation = b->Add(b->Load("tapeCellPointer"),
                               b->ConstInt64(tapeCellPointerOffset));
      b->Store("tapeCellPointer", pointerLocation);
      tapeCellPointerOffset = 0;

      b->IfThenElse(&openBuilder, &closeBuilder,
                    b->NotEqualTo(b->ConstInt8(0),
                                  b->LoadAt(tapeCellPointerType,
                                            b->Load("tapeCellPointer"))));
      b = closeBuilder;

      openStack.pop();
      closeStack.pop();
      break;
    default:
      // all non command characters are considered comments
      break;
    }
  }
  // b->Return();
  b->Return(ConstInt32(69));
  return true;
}

} // namespace bf

void printHelpInfo(char *program, std::ostream &out) {
  out << "OMR BrainF*** Interpreter\n";
  out << "  Usage: " << program << " [options] filename\n";
  out << std::endl;
  out << "  -h, --help          Print this help information.\n";
  out << "  -s <server>,\n";
  out << "  --server <server>   Specify a compilation server to use.\n";
  out << std::endl;
  out << "Fork it on github: https://github.com/youngar/omr-brainfuck\n";
}

int main(int argc, char **argv) {

  omrthread_init_library();

  std::string filename = "";
  std::string server = "";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0) {
      printHelpInfo(argv[0], std::cout);
    } else if (strcmp(argv[i], "--help") == 0) {
      printHelpInfo(argv[0], std::cout);
    } else if (strcmp(argv[i], "-s") == 0) {
      server = argv[++i];
    } else if (strcmp(argv[i], "--server") == 0) {
      server = argv[++i];
    } else {
      filename = argv[i];
    }
  }

  if (filename.empty()) {
    exit(EXIT_SUCCESS);
  }

  // Open the file
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1) {
    std::cerr << " Cannot open file: \"" << filename << "\"\n";
    perror("");
    exit(EXIT_FAILURE);
  }

  // Get file size
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    std::cerr << " Cannot stat file: \"" << filename << "\"\n";
    perror("");
    exit(EXIT_FAILURE);
  }
  std::size_t length = sb.st_size;

  // mmap the code
  char *addr = (char *)mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    std::cerr << " Cannot mmap file: \"" << filename << "\"\n";
    perror("");
    exit(EXIT_FAILURE);
  }

  if (!initializeJit()) {
    exit(EXIT_FAILURE);
  }

  bf::BrainFuckVM bf;
  bf.setServer(server);
  bf.runByteCodes(addr, length);

  munmap(addr, length);
  close(fd);

  omrthread_shutdown_library();
  shutdownJit();

  exit(EXIT_SUCCESS);
}
