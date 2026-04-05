# Interactive Backup Management System

This project is an interactive command-line application written in C for Linux environments. It allows users to manage directory backups by creating real-time mirrors, monitoring folders for ongoing changes, and restoring data on demand.

 ## Key Features

Real-time Monitoring: Once a backup is initiated, the program uses the Linux inotify API to actively watch the source directory. Any newly created files, modifications, or deletions are instantly mirrored in the target directory.

Parallel Execution: The main command-line interface remains responsive at all times. Each backup task is spawned as an independent background process, meaning multiple backups can run and sync simultaneously without blocking user input.

Smart Symlink Handling: The system correctly processes symbolic links. If a symlink points to a location inside the source directory, the backup creates a corresponding symlink pointing to the equivalent location in the backup directory. External links are copied as-is.

Optimized Restoration: When restoring a backup, the program compares file sizes and modification times between the source and target. It only copies files that are missing or have been changed, significantly speeding up the recovery process.

Graceful Termination: The program handles system signals cleanly, ensuring that all background monitoring processes are safely terminated and resources are freed when the application closes.

## Supported Commands

Upon launching the program, you will be presented with an interactive prompt where you can use the following commands. Note that if your directory paths contain spaces, you must enclose them in double quotes (e.g., "my folder").

#### 1.  Create a Backup

**add  <target path 1> [target path 2] ...**

Initiates a backup of the source folder into one or more target locations.

If the target directory does not exist, it will be created.

If it already exists, it must be empty.

The program will immediately perform an initial copy of the entire directory structure and then begin silently monitoring the source folder for any future changes.

The program prevents you from backing up a directory into itself to avoid infinite loops.

#### 2.  List Active Backups

**list**

Prints a summary of all currently active background monitoring tasks, displaying the source directories and their corresponding backup destinations.

#### 3.  Stop Monitoring

**end  <target path 1> [target path 2] ...**

Terminates the active monitoring process for the specified backup(s). The files that have already been backed up in the target directory are left intact, but future changes to the source folder will no longer be synchronized.

#### 4.  Restore a Backup

**restore**

Restores the contents of a backup back into a specified directory. The program will recreate the exact state of the backup by copying files over and deleting any extraneous files from the target directory that do not exist in the backup. This command blocks the prompt until the restoration is fully completed.

#### 5.  Exit the Program

**exit**

Safely terminates the application. This command automatically stops all active background backup processes, cleans up zombie processes, and frees allocated memory before exiting.