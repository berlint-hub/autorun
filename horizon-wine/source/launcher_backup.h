/*
 * A copy of the launcher's settings: the look, the arguments, the controls every
 * game uses, the runtime's defaults, the settings of games on USB, and each
 * game's own files beside it. It is the rollback a card needs after a game was
 * tuned into not starting at all.
 *
 * The copy sits in <runtime>/backups/<stamp>/, with a manifest that says where
 * every file came from, so restoring puts each one back where it was. The
 * library itself is not part of it: games, artwork and play time are content,
 * not settings, and stay as they are.
 */
#ifndef WINE_NX_LAUNCHER_BACKUP_H
#define WINE_NX_LAUNCHER_BACKUP_H

#include <stddef.h>

/* How many are kept: the oldest goes when another one is taken. */
#define LAUNCHER_BACKUP_MAX 5
/* "2026-10-26-1745", the suffix of a second one in the same minute, and the
 * terminator. */
#define LAUNCHER_BACKUP_NAME 32

struct launcher_backup
{
    char name[LAUNCHER_BACKUP_NAME];
    unsigned int files;            /* the files it kept */
    unsigned long long bytes;      /* what they take on the card */
};

/* "2026-10-26-1745" for seconds from the epoch, in UTC: the console's clock is
 * the one it was set to, so this is the time the user saw. */
void launcher_backup_stamp( char *out, size_t size, unsigned int now );

/* "2026-10-26 17:45" for a backup's name, for the screens that list them. */
void launcher_backup_title( char *out, size_t size, const char *name );

/* "12 files, 1.4 MB": what a backup holds, for the same screens. */
void launcher_backup_detail( char *out, size_t size, const struct launcher_backup *backup );

/* Takes the copy. `programs` are the executables in the library, whose own
 * files are beside them. `keep` says how many are kept afterwards: 0 for
 * LAUNCHER_BACKUP_MAX, -1 to keep everything (a copy taken to be undone, whose
 * oldest may be the one the caller is about to read). Returns 1 and fills
 * `out`, or 0 with `error` set. */
int launcher_backup_create( const char *runtime_dir, const char *stamp,
                            const char *const *programs, int program_count, int keep,
                            struct launcher_backup *out, char *error, size_t error_size );

/* Drops the oldest until `keep` are left. 0 or less keeps LAUNCHER_BACKUP_MAX. */
void launcher_backup_prune( const char *runtime_dir, int keep );

/* The backups on the card, newest first. Returns 1 even when there are none. */
int launcher_backup_list( const char *runtime_dir, struct launcher_backup *backups,
                          int max, int *count );

/* How many there are, without reading them: what a row that lists them shows. */
int launcher_backup_count( const char *runtime_dir );

/* Puts every file of the backup back where it came from, and counts them.
 * Returns 1, or 0 with `error` set to the file that could not be put back; the
 * files before that one stay put back, and `restored` counts them. */
int launcher_backup_restore( const char *runtime_dir, const char *name,
                             unsigned int *restored, char *error, size_t error_size );

#endif
