#include <string.h>
#define NOB_IMPLEMENTATION
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "nob.h"

#ifdef _WIN32
#define OS_SEP "\\"
#define EXE_EXT ".exe"
#define OBJ_EXT ".obj"
#else
#define OS_SEP "/"
#define EXE_EXT
#define OBJ_EXT ".o"
#endif

#define BUILD_DIR "build" OS_SEP
#define BIN_DIR   "bin"   OS_SEP
#define SRC_DIR   "src"   OS_SEP

#define EXECUTABLE "today" EXE_EXT

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
    const char* obj_path = nob_temp_sprintf(BUILD_DIR "%s" OBJ_EXT, *s);
#if defined(_MSC_VER)
    nob_cmd_append(&cmd,"/nologo");
    nob_cmd_append(&cmd,"/W4");
    nob_cmd_append(&cmd,"/DEBUG");
    nob_cmd_append(&cmd,nob_temp_sprintf("/Fo:%s", obj_path));
    nob_cmd_append(&cmd,"/c");
    nob_cmd_append(&cmd, nob_temp_sprintf(SRC_DIR "%s", *s));
    nob_cmd_append(&cmd,"/DNDEBUG");
    // nob_cmd_append(&cmd,"-DLOG_NOCOLOR");
#elif defined(__GNUC__) || defined(__MINGW32__)
    nob_cmd_append(&cmd,"-Wall");
    nob_cmd_append(&cmd,"-Wextra");
    nob_cmd_append(&cmd,"-g3");
    nob_cmd_append(&cmd,"-I/usr/local/ssl/include");
    nob_cmd_append(&cmd,"-o");
    nob_cmd_append(&cmd,nob_temp_sprintf("%s", obj_path));
    nob_cmd_append(&cmd,"-c");
    nob_cmd_append(&cmd, nob_temp_sprintf(SRC_DIR "%s", *s));
    nob_cmd_append(&cmd,"-DNDEBUG");
    // nob_cmd_append(&cmd,"-DLOG_NOCOLOR");
#endif
    nob_da_append(&objects, obj_path);
    if (!nob_cmd_run(&cmd, .async = &procs)) return 1;
  }

  nob_procs_flush(&procs);

  // link objects
#ifdef _MSC_VER
  nob_cmd_append(&cmd,"link");
  nob_cmd_append(&cmd,"/nologo");
  nob_cmd_append(&cmd,"/DEBUG");
  nob_cmd_append(&cmd,"/OUT:" BIN_DIR EXECUTABLE);
  nob_cmd_append(&cmd,"Wininet.lib");
#elif defined(__GNUC__) || defined(__MINGW32__)
  nob_cmd_append(&cmd,"gcc");
  nob_cmd_append(&cmd,"-g3");
  nob_cmd_append(&cmd,"-o");
  nob_cmd_append(&cmd,BIN_DIR EXECUTABLE);
#endif
  nob_da_foreach(const char*, o, &objects) {
    nob_cmd_append(&cmd, *o);
  }
#if defined(__GNUC__) || defined(__MINGW32__)
  nob_cmd_append(&cmd,"-L/usr/local/ssl/lib64");
  nob_cmd_append(&cmd,"-lssl");
  nob_cmd_append(&cmd,"-lcrypto");
#endif
  if (!nob_cmd_run(&cmd)) return 1;

  return 0;
}
