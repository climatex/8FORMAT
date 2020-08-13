// 8FORMAT (c) J. Bogin
// Compile for DOS (unsigned char: byte, unsigned int: word)

#include <stdlib.h>

int main(int argc, char* argv[])
{ 
  PrintSplash();
  ParseCommandLine(argc, argv);

  DoOperations();
 
  //The program quits before here, but main() shall return something on paper
  return 0;
}


