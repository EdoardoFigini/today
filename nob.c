#include <string.h>
#define NOB_IMPLEMENTATION
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "nob.h"

#ifdef _WIN32
#define OS_SEP "\\"
#else
#define OS_SEP "/"
#endif

#define BUILD_DIR "build" OS_SEP
#define BIN_DIR   "bin"   OS_SEP
#define SRC_DIR   "src"   OS_SEP

#define EXECUTABLE "today"

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  nob_mkdir_if_not_exists(BUILD_DIR);
  nob_mkdir_if_not_exists(BIN_DIR);

  Nob_Cmd cmd = { 0 };
  Nob_Procs procs = { 0 };

  Nob_File_Paths sources = { 0 };
  Nob_File_Paths objects = { 0 };

  nob_read_entire_dir(SRC_DIR, &sources);

  // compile objects
  nob_da_foreach(const char*, s, &sources) {
    if (memcmp(".c", *s + strlen(*s) - 2, 2) != 0) continue;
    nob_cc(&cmd);
#ifdef _MSC_VER
    const char* obj_path = nob_temp_sprintf(BUILD_DIR "%s.obj", *s);
    nob_cmd_append(&cmd,"/nologo");
    nob_cmd_append(&cmd,nob_temp_sprintf("/Fo:%s", obj_path));
    nob_cmd_append(&cmd,"/c");
    nob_cmd_append(&cmd, nob_temp_sprintf(SRC_DIR "%s", *s));
    nob_da_append(&objects, obj_path);
#endif
    if (!nob_cmd_run(&cmd, .async = &procs)) return 1;
  }

  nob_procs_flush(&procs);

  // link objects
#ifdef _MSC_VER
  nob_cmd_append(&cmd,"link");
  nob_cmd_append(&cmd,"/nologo");
  nob_cmd_append(&cmd,"/OUT:" BIN_DIR EXECUTABLE ".exe");
  nob_da_foreach(const char*, o, &objects) {
    nob_cmd_append(&cmd, *o);
  }
#endif
  if (!nob_cmd_run(&cmd)) return 1;

  return 0;
}
