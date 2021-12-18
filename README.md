# ECSE427 Project 3 (Simple File System) - Fall 2021

---

## How to run tests?

In this project we use `make` to build our project.

1. Navigate to the project root directory.
2. Run `make clean` (Not necessary, but it is neat to clean up before running tests)
3. Modify the `Makefile` document, uncomment the test you wish to run.
4. Run `make`
5. Run `./sfs`. If you are testing the fuse tests, you also need to specify the mount point as `./sfs <mountpoint>`
6. Run `make clean`

## File Structure

```text
.
├── Makefile        // Build configurations.
├── README.md
├── disk_emu.c      // Disk emulators
├── disk_emu.h
├── fuse_wrap_new.c // Fuse wrappers (Initialize SFS from scratch)
├── fuse_wrap_old.c // Fuse wrappers (Initialize SFS from existing configurations)
├── sfs_api.c       // The SFS.
├── sfs_api.h
├── sfs_test0.c
├── sfs_test1.c
└── sfs_test2.c
```

## Notice

The project was developed on **Linux** where **FUSE** was available. Because **MacOS** does not have **FUSE** installed,
the building process will fail. If you insist on using **MacOS**, please modify the `Makefile` configuration yourself.

While you are running tests 0, 1, and 2, you may notice colourful console outputs, such as the one below.

```text
ING TIAN: [SFS ERROR] TIMESTAMP: 1639859171 --> Attempt to close a file that has already been closed.
```

These outputs do not indicate the failure of tests but rather for debugging purposes. Please ignore such outputs.
