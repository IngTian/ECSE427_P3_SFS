cmake_minimum_required(VERSION 3.10)
project(test)

# Find the fuse module.
find_package(PkgConfig)
pkg_check_modules(FUSE REQUIRED fuse)


add_executable(test0 sfs_test0.c disk_emu.c sfs_api.c)
add_executable(test1 sfs_test1.c disk_emu.c sfs_api.c)
add_executable(test2 sfs_test2.c disk_emu.c sfs_api.c)
add_executable(test3 fuse_wrap_old.c disk_emu.c sfs_api.c)
target_link_libraries(test3 ${FUSE_LIBRARIES})
target_include_directories(test3 PUBLIC ${FUSE_INCLUDE_DIRS})
target_compile_options(test3 PUBLIC ${FUSE_CFLAGS_OTHER})
target_compile_options(test3 PRIVATE -D_FILE_OFFSET_BITS=64 -c -g -ansi -pedantic -Wall -std=gnu99)
link_libraries(pkg-config=fuse --cflags --libs)
add_executable(test4 fuse_wrap_new.c disk_emu.c sfs_api.c)
