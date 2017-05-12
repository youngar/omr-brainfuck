#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <stack>
#include <string>
#include <unistd.h>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "Jit.hpp"
#include "ilgen/MethodBuilder.hpp"
#include "ilgen/TypeDictionary.hpp"
#include "ilgen/TypeDictionary.hpp"

namespace bf {

using TapeCell = uint8_t;

class BrainFuckVM : public TR::MethodBuilder {
  public:
  explicit BrainFuckVM(TR::TypeDictionary *types);
  void runByteCodes(char *byteCodes, std::size_t numberOfByteCodes);

  private:
  static TapeCell getCharacter();
  static void putCharacter(TapeCell character);
  bool buildIL() override;

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

void BrainFuckVM::putCharacter(TapeCell character) {
  std::cout << character;
}

BrainFuckVM::BrainFuckVM(TR::TypeDictionary *types)
  : MethodBuilder(types),
  tapeCellType(Int8),
  tapeCellPointerType(types->PointerTo(Int8)) {

  DefineLine(LINETOSTR(__LINE__));
  DefineFile(__FILE__);
  DefineName("brainfuck");

  /* tell the compiler that compiled programs do no return anything */
  DefineReturnType(NoType);
  DefineLocal("tapeCellPointer", tapeCellPointerType);
  DefineFunction("putCharacter", __FILE__, "putCharacter", reinterpret_cast<void*>(&bf::BrainFuckVM::putCharacter), NoType, 1, tapeCellType);
  DefineFunction("getCharacter", __FILE__, "getCharacter", reinterpret_cast<void*>(&bf::BrainFuckVM::getCharacter), tapeCellType, 0);
  AllLocalsHaveBeenDefined();
}

void BrainFuckVM::runByteCodes(char *byteCodes, std::size_t numberOfByteCodes) {
  byteCodes_ = byteCodes;
  numberOfByteCodes_ = numberOfByteCodes;

  /* Zero the tape */
  std::memset(tape, 0, tapeSize);

  uint8_t* entry;
  int rc = compileMethodBuilder(this, &entry);
  if (rc != 0) {
    std::cout << "Compilation failed" << std::endl;
    return;
  }

  void (*compiledFunction)() = reinterpret_cast<decltype(compiledFunction)>(entry);
  (*compiledFunction)();
}

bool BrainFuckVM::buildIL() {
  std::stack<TR::IlBuilder*> openStack;
  std::stack<TR::IlBuilder*> closeStack;
  TR::IlBuilder *openBuilder;
  TR::IlBuilder *closeBuilder;
  TR::IlBuilder *b  = this;

  b->Store("tapeCellPointer", b->ConstAddress(&tape));
  for (const char *byteCode = byteCodes_; byteCode < &byteCodes_[numberOfByteCodes_]; byteCode++) {
    switch (*byteCode) {
      case '>': // Move the pointer forward by 1.
        b->Store("tapeCellPointer", b->Add(b->Load("tapeCellPointer"), b->ConstInt64(1)));
        break;
      case '<': // Move the pointer backward by 1.
        b->Store("tapeCellPointer", b->Sub(b->Load("tapeCellPointer"), b->ConstInt64(1)));
        break;
      case '+': // Increment the cell at the current pointer.
        b->StoreAt(b->Load("tapeCellPointer"), b->Add(b->LoadAt(tapeCellPointerType, b->Load("tapeCellPointer")), b->ConstInt8(1)));
        break;
      case '-': // Decrements the cell at the current pointer.
        b->StoreAt(b->Load("tapeCellPointer"), b->Sub(b->LoadAt(tapeCellPointerType, b->Load("tapeCellPointer")), b->ConstInt8(1)));
        break;
      case '.': // Output the character at the current cell.
        b->Call("putCharacter", 1, b->LoadAt(tapeCellPointerType, b->Load("tapeCellPointer")), b->ConstInt8(1));
        break;
      case ',': // Read a character into the current cell (for our purposes, EOF produces 0).
        b->StoreAt(b->Load("tapeCellPointer"), b->Call("getCharacter", 0));
        break;
      case '[': // If the current cell is zero, skips to the matching ] later on.
        openBuilder = OrphanBuilder();
        closeBuilder = OrphanBuilder();
        b->IfThenElse(&openBuilder, &closeBuilder,
            b->NotEqualTo(b->ConstInt8(0), b->LoadAt(tapeCellPointerType, b->Load("tapeCellPointer"))));
        b = openBuilder;
        openStack.push(openBuilder);
        closeStack.push(closeBuilder);
        break;
      case ']': // If the current cell is non-zero, skips to the matching [ earlier on.
        closeBuilder = closeStack.top();
        openBuilder = openStack.top();
        b->IfThenElse(&openBuilder, &closeBuilder,
            b->NotEqualTo(b->ConstInt8(0), b->LoadAt(tapeCellPointerType, b->Load("tapeCellPointer"))));
        b = closeBuilder;
        openStack.pop();
        closeStack.pop();
        break;
      default:
        // all non command characters are considered comments
        break;
    }
  }
  b->Return();
  std::cout << "done compiling" << std::endl;
  return true;
}

} // namespace bf

int main(int argc, char** argv)
{
  std::cout << "OMR BrainFuck Interpreter" << std::endl;

  std::string filename;
  if (argc > 1) {
    filename = argv[1];
  }

  boost::interprocess::file_mapping mapping(filename.c_str(), boost::interprocess::read_only);
  boost::interprocess::mapped_region mapped_rgn(mapping, boost::interprocess::read_only);
  auto const mmaped_data = static_cast<char*>(mapped_rgn.get_address());
  std::size_t mmap_size = mapped_rgn.get_size();

  if (!initializeJit()) {
    exit(EXIT_FAILURE);
  }

  TR::TypeDictionary types;
  bf::BrainFuckVM brainFuckVM(&types);
  brainFuckVM.runByteCodes(mmaped_data, mmap_size);

  shutdownJit();

  exit(EXIT_SUCCESS);
}
