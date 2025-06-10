/* 매핑을 통해 파일에 데이터를 쓰고, 파일의 매핑을 해제한 후,
  read 시스템 호출을 사용하여 파일의 데이터를 다시 읽어 확인합니다. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define ACTUAL ((void *) 0x10000000)

void
test_main (void)
{
  int handle;
  void *map;
  char buf[1024];

  /* Write file via mmap. */
  CHECK ((handle = open ("large.txt")) > 1, "open \"large.txt\"");
  CHECK ((map = mmap (ACTUAL, 4096, 0, handle, 0)) != MAP_FAILED, "mmap \"large.txt\" with writable=0");
  msg ("about to write into read-only mmap'd memory");
  *((int *)map) = 0;
  msg ("Error should have occured");
  munmap (map);
  close (handle);
}
