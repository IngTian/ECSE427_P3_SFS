cmake_minimum_required(VERSION 3.13)
project(test)

add_executable(test0 sfs_test0.c disk_emu.c sfs_api.c)
add_executable(test1 sfs_test1.c disk_emu.c sfs_api.c)
add_executable(test2 sfs_test2.c disk_emu.c sfs_api.c)
