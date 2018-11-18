# Btreefilesystem
A filesystem implemented using B+ trees for file/directory indexing.

This is my first attempt at creating a filesystem of any kind. I came up with this code in a time span of approximately 1 week, so not many features exist. As for what exists, it works pretty well in common scenarios. There might be a bug in the find_parent function, so it might crash during some key promotion in some corner case. Use at your own discretion. But two level B+ tree is ensured to work, as per preliminary testing. Feel free to contribute by sending pull requests to try and fix the issue.

Max. file size = 4GB + 4MB + 13 * 4KB, due to implementing direct, single indirect and double indirect blocks in 4KB bs.
0th index element in the inode points to a stat file containing metadata on the file/directory and what kind of item this is: file or a folder. Stat files are not visible in the userspace.

The image file is a binary file created using the command:
    dd bs=4K count=512K if=/dev/zero of=./part1.img
to create a 2GB file containing zeroes.

A B+ tree is used to index files and directories in a manner that preserves directory localization, i.e., files/directories belonging to the same directory exist grouped together. This eliminates the need to maintain a separate structure for file/directory hierarchy.

Present functionality:
  - Format (makefs)
  - mount/remount
  - Set label for filesystem (setlabel <max. 8 character long string>)
  - Create empty files (newfile <name>)
  - Create a batch of empty files (batch_create_files <number_of_files>)
  - Create directories (mkdir <name>)
  - Show pwd (pwd)
  - List files/directories in pwd (ls)
  - Find file/directory (find <name>)
  - Change directory (cd <directory_name or ..>)
  - Import file from local directory into the filesystem in the image (import <from> <to>) - both strings without spaces
  - Export file from the filesystem image to the local directory (export <from> <to>) - again, no spaces in filenames
  - Debug functions:
    * debug_showroot
    * debug_show_filled_blocks (Why? Because I can!)
    * debug_inorder (only shows leaf elements)

What I would like to implement with time:
  - Deletion of files and directories
  - Fixing find_parent(...)
  - A systematic redistribution algorithm
  - Symbolic links and relative links
  - Reworking block size and other stuff like changing int to long int or long long int, etc.
  - Copying files and moving
  - Recursive import and export files/directories
  - md5sum for files
  - Changes so as to make it work with file descriptors like /dev/sdX
  - Linux module for this filesystem
  - Concurrency control (using a simple mutex variable, maybe)
  - Full file system encryption
