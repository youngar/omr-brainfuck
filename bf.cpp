#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stack>
#include <string>

#include "Jit.hpp"
#include "ilgen/JitBuilderRecorder.hpp"
#include "ilgen/JitBuilderRecorderTextFile.hpp"
#include "ilgen/MethodBuilder.hpp"
#include "ilgen/TypeDictionary.hpp"
#include "imperium/imperium.hpp"

namespace bf {

using TapeCell = uint8_t;

class BrainFuckVM : public TR::MethodBuilder {
public:
  explicit BrainFuckVM(TR::TypeDictionary *types,
                       TR::JitBuilderRecorder *recorder = nullptr);
  void runByteCodes(char *byteCodes, std::size_t numberOfByteCodes);
  void andrew(char *byteCodes, std::size_t numberOfByteCodes, char * fileName);

private:
  static TapeCell getCharacter();
  static void putCharacter(TapeCell character);
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

  /* brainfuck mandates a minimum tape size */
  static const std::size_t tapeSize = 30000;
  TapeCell tape[tapeSize];
  char *byteCodes_ = nullptr;
  std::size_t numberOfByteCodes_ = 0;
};

TapeCell BrainFuckVM::getCharacter() {
  TapeCell input;
  std::cin >> input;
  return input;
}

void BrainFuckVM::putCharacter(TapeCell character) { std::cout << character; }

BrainFuckVM::BrainFuckVM(TR::TypeDictionary *types,
                         TR::JitBuilderRecorder *recorder)
    : MethodBuilder(types, recorder), tapeCellType(Int8),
      tapeCellPointerType(types->PointerTo(Int8)) {
  DefineLine(LINETOSTR(__LINE__));
  DefineFile(__FILE__);
  DefineName("brainfuck");

  /* tell the compiler that compiled programs do no return anything */
  // DefineReturnType(NoType);
  DefineReturnType(Int32);
  DefineLocal("dummy", Int8);
  DefineLocal("tapeCellPointer", tapeCellPointerType);
  DefineFunction("putCharacter", __FILE__, "putCharacter",
                 reinterpret_cast<void *>(&bf::BrainFuckVM::putCharacter),
                 NoType, 1, tapeCellType);
  DefineFunction("getCharacter", __FILE__, "getCharacter",
                 reinterpret_cast<void *>(&bf::BrainFuckVM::getCharacter),
                 tapeCellType, 0);
  AllLocalsHaveBeenDefined();
}

void BrainFuckVM::andrew(char *byteCodes, std::size_t numberOfByteCodes, char *fileName) {
  byteCodes_ = byteCodes;
  numberOfByteCodes_ = numberOfByteCodes;

  uint8_t *entryPoint;
  OMR::Imperium::ClientChannel client("localhost:50055");
  client.requestCompileSync(fileName, &entryPoint, this);

  std::cout << (void *)entryPoint << std::endl;

  /* Zero the tape */
  std::memset(tape, 0, tapeSize);

  uint32_t (*compiledFunction)() =
    reinterpret_cast<decltype(compiledFunction)>(entryPoint);
  (*compiledFunction)();

  client.shutdown();

  // uint8_t *entry;
  // int rc = compileMethodBuilder(this, &entry);
  // if (rc != 0) {
  //   std::cout << "Compilation failed" << std::endl;
  //   return;
  // }

  // void (*compiledFunction)() =
  //     reinterpret_cast<decltype(compiledFunction)>(entry);
  // (*compiledFunction)();
}

void BrainFuckVM::runByteCodes(char *byteCodes, std::size_t numberOfByteCodes) {
  byteCodes_ = byteCodes;
  numberOfByteCodes_ = numberOfByteCodes;

  /* Zero the tape */
  std::memset(tape, 0, tapeSize);

  uint8_t *entry;
  int rc = compileMethodBuilder(this, &entry);
  if (rc != 0) {
    std::cout << "Compilation failed" << std::endl;
    return;
  }

  void (*compiledFunction)() =
      reinterpret_cast<decltype(compiledFunction)>(entry);
  (*compiledFunction)();
}

/* Cache local tape values */
TR::IlValue *BrainFuckVM::getLocal(TR::IlBuilder *b,
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

void BrainFuckVM::setLocal(TR::IlBuilder *b,
                           std::map<int, TR::IlValue *> &locals, int local,
                           TR::IlValue *value) {
  locals[local] = value;
}

void BrainFuckVM::commitLocalsToTape(TR::IlBuilder *b,
                                     std::map<int, TR::IlValue *> &locals) {
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    TR::IlValue *pointerLocation =
        b->Add(b->Load("tapeCellPointer"), b->ConstInt64(it->first));
    b->StoreAt(pointerLocation, it->second);
  }

  locals.clear();
}

bool BrainFuckVM::buildIL() {
  std::stack<TR::IlBuilder *> openStack;
  std::stack<TR::IlBuilder *> closeStack;
  std::map<int, TR::IlValue *> locals;
  TR::IlBuilder *openBuilder;
  TR::IlBuilder *closeBuilder;
  TR::IlBuilder *b = this;

  TR::IlValue *pointerLocation = nullptr;
  b->Store("tapeCellPointer", b->ConstAddress(&tape));
  int64_t tapeCellPointerOffset = 0;

  b->Call("putCharacter", 1, b->ConstInt8(97));
  b->Call("putCharacter", 1, b->ConstInt8(98));

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
  b->Return(ConstInt32(69));
  std::cout << "done compiling" << std::endl;
  return true;
}

} // namespace bf

extern bool jitBuilderShouldCompile;

int main(int argc, char **argv) {
bool jitBuilderShouldCompile = true;

  omrthread_init_library();

  if (!initializeJit()) {
    exit(EXIT_FAILURE);
  }

  std::string filename;
  if (argc > 1) {
    filename = argv[1];
  }

  boost::interprocess::file_mapping mapping(filename.c_str(),
                                            boost::interprocess::read_only);
  boost::interprocess::mapped_region mapped_rgn(mapping,
                                                boost::interprocess::read_only);
  auto const mmaped_data = static_cast<char *>(mapped_rgn.get_address());
  std::size_t mmap_size = mapped_rgn.get_size();

  char *fileName = std::tmpnam(NULL);
  TR::TypeDictionary types;
  TR::JitBuilderRecorderTextFile recorder(NULL, fileName);

  bf::BrainFuckVM brainFuckVM(&types, &recorder);
  // brainFuckVM.runByteCodes(mmaped_data, mmap_size);
  brainFuckVM.andrew(mmaped_data, mmap_size, fileName);


  omrthread_shutdown_library();
  shutdownJit();

  exit(EXIT_SUCCESS);
}
