/* Maps and unmaps a file and verifies that the mapped region is
   inaccessible afterward. */

#include <syscall.h>
#include "tests/vm/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

#define ACTUAL ((void *) 0x10000000)


void
test_main (void)
{
  int handle;
  void *map;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  CHECK ((map = mmap (ACTUAL, 0x2000, 0, handle, 0)) != MAP_FAILED, "mmap \"sample.txt\"");
  // msg("actual %d", *(int *)0x10000000);
  msg ("memory is readable %d", *(int *) ACTUAL);
  msg ("memory is readable %d", *(int *) ACTUAL + 0x1000);

  munmap (map);
  // msg("%d", *(int *) (ACTUAL + 0x1000));
  fail ("unmapped memory is readable (%d)", *(int *) (ACTUAL + 0x1000));
  fail ("unmapped memory is readable (%d)", *(int *) (ACTUAL));
}
