#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "launcher_backup.h"
#include "launcher_list.h"
#include "launcher_settings.h"

/* A game in a deep folder keeps its settings beside it, and the manifest has to
 * keep that whole path: longer than the launcher's own buffers. */
#define BACKUP_PATH 1024
/* Files one backup may hold: every game's settings and options, with room to
 * spare for a card fuller than the library shows. */
#define BACKUP_FILES 4096

/* The launcher's own settings, beside the runtime: the look, the arguments, the
 * controls, and the defaults the runtime keeps for games without settings of
 * their own. */
static const char *const launcher_files[] =
{
    "launcher.txt", "keys.txt", "args.txt", "config/keys.txt", "config/settings.json"
};
/* Each game's own files, named after its executable. */
static const char *const program_suffixes[] = { ".args.txt", ".keys.txt", ".box64.txt" };

struct backup_writer
{
    char directory[BACKUP_PATH];
    FILE *manifest;
    unsigned int files;
    unsigned long long bytes;
};

/* What a walk of a manifest does with one of its files: 1 to go on, 0 to stop. */
typedef int (*backup_file_fn)( void *opaque, unsigned int index, const char *path );

static int set_error( char *error, size_t size, const char *format, const char *path )
{
    if (error && size) snprintf( error, size, format, path );
    return 0;
}

static int join_path( char *out, size_t size, const char *directory, const char *name )
{
    int written;

    if (!directory[0]) written = snprintf( out, size, "%s", name );
    else if (directory[strlen( directory ) - 1] == '/') written = snprintf( out, size, "%s%s", directory, name );
    else written = snprintf( out, size, "%s/%s", directory, name );
    return written > 0 && (size_t)written < size;
}

/* The folder a manifest entry is kept in. */
static int files_folder( char *out, size_t size, const char *directory )
{
    return join_path( out, size, directory, "files" );
}

/* The copy of a manifest entry, 0000, 0001, … under files/. */
static int entry_path( char *out, size_t size, const char *directory, unsigned int index )
{
    char folder[BACKUP_PATH];
    int length;

    if (!files_folder( folder, sizeof(folder), directory )) return 0;
    length = snprintf( out, size, "%s/%04u", folder, index );
    return length > 0 && (size_t)length < size;
}

/* 1 when made, 0 when it was already there, -1 when it could not be. */
static int make_folder( const char *path )
{
    if (!mkdir( path, 0777 )) return 1;
    return errno == EEXIST ? 0 : -1;
}

static int backups_root( const char *runtime_dir, char *out, size_t size )
{
    return join_path( out, size, runtime_dir, "backups" );
}

static int backup_folder( const char *runtime_dir, const char *name, char *out, size_t size )
{
    char root[BACKUP_PATH];

    if (!backups_root( runtime_dir, root, sizeof(root) )) return 0;
    return join_path( out, size, root, name );
}

/* A name this module made: digits and dashes, so a hand-edited card cannot turn
 * it into a path somewhere else. */
static int backup_name_valid( const char *name )
{
    size_t i, length = strlen( name );

    if (!length || length >= LAUNCHER_BACKUP_NAME) return 0;
    for (i = 0; i < length; i++)
        if ((name[i] < '0' || name[i] > '9') && name[i] != '-') return 0;
    return 1;
}

/* 1 when copied, 0 when the source is not there, -1 on a failure. */
static int copy_file( const char *from, const char *to, unsigned long long *bytes,
                      char *error, size_t error_size )
{
    char buffer[8192];
    unsigned long long total = 0;
    FILE *in, *out;
    size_t got;

    if (!(in = fopen( from, "rb" ))) return errno == ENOENT ? 0 : -1;
    if (!(out = fopen( to, "wb" )))
    {
        fclose( in );
        set_error( error, error_size, "Could not write %s", to );
        return -1;
    }
    while ((got = fread( buffer, 1, sizeof(buffer), in )) > 0)
    {
        if (fwrite( buffer, 1, got, out ) != got) goto failed;
        total += got;
    }
    if (ferror( in )) goto failed;
    if (fclose( out ))
    {
        fclose( in );
        remove( to );
        set_error( error, error_size, "Could not write %s", to );
        return -1;
    }
    fclose( in );
    if (bytes) *bytes += total;
    return 1;
failed:
    fclose( out );
    fclose( in );
    remove( to );
    set_error( error, error_size, "Could not copy %s", from );
    return -1;
}

/* Puts one of the original's files in the backup and its path in the manifest. */
static int take_file( struct backup_writer *writer, const char *from, char *error, size_t error_size )
{
    char to[BACKUP_PATH];
    int copied;

    if (writer->files >= BACKUP_FILES) return set_error( error, error_size, "Too many files to back up: %s", from );
    if (!entry_path( to, sizeof(to), writer->directory, writer->files ))
        return set_error( error, error_size, "Too long a path to back up: %s", from );
    copied = copy_file( from, to, &writer->bytes, error, error_size );
    if (copied == 0) return 1;   /* this game keeps nothing there */
    if (copied < 0) return 0;
    if (fprintf( writer->manifest, "%04u\t%s\n", writer->files, from ) < 0)
    {
        remove( to );
        return set_error( error, error_size, "Could not write the list of the backup: %s", from );
    }
    writer->files++;
    return 1;
}

static int take_program_files( struct backup_writer *writer, const char *executable,
                               char *error, size_t error_size )
{
    char path[BACKUP_PATH];
    size_t i;

    for (i = 0; i < sizeof(program_suffixes) / sizeof(program_suffixes[0]); i++)
        if (launcher_sibling_path( executable, program_suffixes[i], path, sizeof(path) ) &&
            !take_file( writer, path, error, error_size )) return 0;
    return 1;
}

/* Settings of games on USB, which live under the runtime rather than beside the
 * game: the whole folder, since the manifest keeps the paths anyway. */
static int take_settings_folder( struct backup_writer *writer, const char *runtime_dir,
                                 char *error, size_t error_size )
{
    char folder[BACKUP_PATH], path[BACKUP_PATH];
    struct dirent *entry;
    DIR *dir;

    if (!join_path( folder, sizeof(folder), runtime_dir, "program-settings" ) ||
        !(dir = opendir( folder ))) return 1;   /* nothing kept there yet */
    while ((entry = readdir( dir )))
    {
        struct stat st;

        if (entry->d_name[0] == '.') continue;
        if (!join_path( path, sizeof(path), folder, entry->d_name ) || stat( path, &st ) || !S_ISREG( st.st_mode )) continue;
        if (!take_file( writer, path, error, error_size ))
        {
            closedir( dir );
            return 0;
        }
    }
    closedir( dir );
    return 1;
}

/* The manifest, one file at a time: "<index>\t<where it came from>". Returns 1
 * with the number of files, or 0 when it cannot be read to its end. */
static int read_manifest( const char *manifest_path, backup_file_fn file, void *opaque, unsigned int *count )
{
    char line[BACKUP_PATH + 32], path[BACKUP_PATH];
    unsigned int index;
    int ok = 1;
    FILE *handle;

    *count = 0;
    if (!(handle = fopen( manifest_path, "r" ))) return 0;
    while (ok && fgets( line, sizeof(line), handle ))
    {
        char *newline = strchr( line, '\n' );

        if (!newline && !feof( handle )) { ok = 0; break; }   /* longer than a path */
        path[0] = 0;
        if (sscanf( line, "%u\t%1023[^\n]", &index, path ) != 2) continue;
        path[strcspn( path, "\r" )] = 0;
        if (!path[0]) continue;
        (*count)++;
        if (!file( opaque, index, path )) ok = 0;
    }
    if (ferror( handle )) ok = 0;
    fclose( handle );
    if (!ok) *count = 0;
    return ok;
}

struct manifest_total
{
    const char *directory;
    unsigned long long bytes;
};

static int sum_file( void *opaque, unsigned int index, const char *path )
{
    struct manifest_total *total = opaque;
    char file[BACKUP_PATH];
    struct stat st;

    (void)path;
    if (!entry_path( file, sizeof(file), total->directory, index )) return 1;
    if (!stat( file, &st )) total->bytes += (unsigned long long)st.st_size;
    return 1;
}

struct restore_state
{
    const char *directory;
    unsigned int restored;
    char *error;
    size_t error_size;
};

static int put_file_back( void *opaque, unsigned int index, const char *path )
{
    struct restore_state *state = opaque;
    char from[BACKUP_PATH], folder[BACKUP_PATH], *slash;

    if (!entry_path( from, sizeof(from), state->directory, index )) return 0;
    /* The folder may be gone: a USB game's settings folder is made again rather
     * than failing the whole restore. */
    snprintf( folder, sizeof(folder), "%s", path );
    if ((slash = strrchr( folder, '/' )))
    {
        *slash = 0;
        make_folder( folder );
    }
    if (copy_file( from, path, NULL, state->error, state->error_size ) <= 0)
    {
        set_error( state->error, state->error_size, "Could not put back %s", path );
        return 0;
    }
    state->restored++;
    return 1;
}

struct remove_state
{
    const char *directory;
};

static int remove_copy( void *opaque, unsigned int index, const char *path )
{
    struct remove_state *state = opaque;
    char file[BACKUP_PATH];

    (void)path;
    if (entry_path( file, sizeof(file), state->directory, index )) remove( file );
    return 1;
}

static void remove_backup( const char *runtime_dir, const char *name )
{
    char directory[BACKUP_PATH], manifest[BACKUP_PATH], folder[BACKUP_PATH];
    struct remove_state state;
    unsigned int count;

    if (!backup_folder( runtime_dir, name, directory, sizeof(directory) ) ||
        !join_path( manifest, sizeof(manifest), directory, "manifest.txt" )) return;
    state.directory = directory;
    read_manifest( manifest, remove_copy, &state, &count );
    remove( manifest );
    if (files_folder( folder, sizeof(folder), directory )) rmdir( folder );
    rmdir( directory );
}

void launcher_backup_stamp( char *out, size_t size, unsigned int now )
{
    /* Days to civil date after Howard Hinnant, from 1970-01-01: no C library
     * calendar is reached, so the time zone cannot make two consoles disagree. */
    unsigned int days = now / 86400, seconds = now % 86400, z = days + 719468;
    unsigned int era = z / 146097, doe = z - era * 146097;
    unsigned int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned int mp = (5 * doy + 2) / 153;
    unsigned int day = doy - (153 * mp + 2) / 5 + 1;
    unsigned int month = mp < 10 ? mp + 3 : mp - 9;
    unsigned int year = yoe + era * 400 + (month <= 2);

    snprintf( out, size, "%04u-%02u-%02u-%02u%02u", year, month, day, seconds / 3600, seconds / 60 % 60 );
}

void launcher_backup_title( char *out, size_t size, const char *name )
{
    /* 2026-10-26-1745 reads better as 2026-10-26 17:45. Anything else is said
     * as it is. */
    if (strlen( name ) >= 15 && name[4] == '-' && name[7] == '-' && name[10] == '-')
        snprintf( out, size, "%.10s %.2s:%.2s", name, name + 11, name + 13 );
    else snprintf( out, size, "%s", name );
}

void launcher_backup_detail( char *out, size_t size, const struct launcher_backup *backup )
{
    char amount[32];

    if (backup->bytes < 1024) snprintf( amount, sizeof(amount), "%llu B", backup->bytes );
    else if (backup->bytes < 1024ull * 1024)
        snprintf( amount, sizeof(amount), "%llu.%llu KB", backup->bytes / 1024,
                  backup->bytes % 1024 * 10 / 1024 );
    else snprintf( amount, sizeof(amount), "%llu.%llu MB", backup->bytes / (1024ull * 1024),
                   backup->bytes % (1024ull * 1024) * 10 / (1024ull * 1024) );
    snprintf( out, size, "%u %s, %s", backup->files, backup->files == 1 ? "file" : "files", amount );
}

int launcher_backup_create( const char *runtime_dir, const char *stamp,
                            const char *const *programs, int program_count,
                            struct launcher_backup *out, char *error, size_t error_size )
{
    char root[BACKUP_PATH], directory[BACKUP_PATH], manifest[BACKUP_PATH], path[BACKUP_PATH], name[LAUNCHER_BACKUP_NAME];
    struct backup_writer writer;
    int attempt, ok = 0;
    size_t i;

    if (out) memset( out, 0, sizeof(*out) );
    if (error && error_size) error[0] = 0;
    if (!runtime_dir[0] || !stamp[0]) return set_error( error, error_size, "There is no folder to keep a backup of: %s", runtime_dir );
    if (!backups_root( runtime_dir, root, sizeof(root) )) return set_error( error, error_size, "Too long a path: %s", runtime_dir );
    if (make_folder( root ) < 0) return set_error( error, error_size, "Could not make %s", root );

    for (attempt = 1; attempt <= 9; attempt++)
    {
        int made;

        if (attempt == 1) snprintf( name, sizeof(name), "%s", stamp );
        else snprintf( name, sizeof(name), "%s-%d", stamp, attempt );
        if (!backup_name_valid( name )) return set_error( error, error_size, "Not a name for a backup: %s", name );
        if (!backup_folder( runtime_dir, name, directory, sizeof(directory) ))
            return set_error( error, error_size, "Too long a path: %s", name );
        if ((made = make_folder( directory )) < 0) return set_error( error, error_size, "Could not make %s", directory );
        if (!made) continue;   /* the same minute twice */
        break;
    }
    if (attempt > 9) return set_error( error, error_size, "Could not name a backup beside %s", stamp );

    memset( &writer, 0, sizeof(writer) );
    snprintf( writer.directory, sizeof(writer.directory), "%s", directory );
    if (!files_folder( path, sizeof(path), directory ) || make_folder( path ) < 0 ||
        !join_path( manifest, sizeof(manifest), directory, "manifest.txt" ) ||
        !(writer.manifest = fopen( manifest, "w" )))
    {
        set_error( error, error_size, "Could not make %s", directory );
        goto done;
    }

    for (i = 0; i < sizeof(launcher_files) / sizeof(launcher_files[0]); i++)
        if (join_path( path, sizeof(path), runtime_dir, launcher_files[i] ) &&
            !take_file( &writer, path, error, error_size )) goto done;
    if (!take_settings_folder( &writer, runtime_dir, error, error_size )) goto done;
    for (i = 0; i < (size_t)program_count; i++)
    {
        if (!programs[i][0]) continue;
        /* A game on USB keeps its settings under the runtime, and the folder
         * below takes them in one go. */
        if (!launcher_settings_on_usb( programs[i] ) &&
            launcher_program_settings_path( runtime_dir, programs[i], path, sizeof(path) ) &&
            !take_file( &writer, path, error, error_size )) goto done;
        if (!take_program_files( &writer, programs[i], error, error_size )) goto done;
    }
    if (fflush( writer.manifest ) || fclose( writer.manifest ))
    {
        writer.manifest = NULL;
        set_error( error, error_size, "Could not write the list of the backup: %s", name );
        goto done;
    }
    writer.manifest = NULL;
    if (!writer.files)
    {
        set_error( error, error_size, "There are no settings to back up yet: %s", runtime_dir );
        goto done;
    }
    ok = 1;
done:
    if (writer.manifest) fclose( writer.manifest );
    if (!ok)
    {
        remove_backup( runtime_dir, name );
        return 0;
    }
    if (out)
    {
        snprintf( out->name, sizeof(out->name), "%s", name );
        out->files = writer.files;
        out->bytes = writer.bytes;
    }

    /* The oldest go once there are more than are kept. */
    {
        struct launcher_backup kept[LAUNCHER_BACKUP_MAX + 8];
        int count, old;

        if (launcher_backup_list( runtime_dir, kept, (int)(sizeof(kept) / sizeof(kept[0])), &count ))
            for (old = LAUNCHER_BACKUP_MAX; old < count; old++) remove_backup( runtime_dir, kept[old].name );
    }
    return 1;
}

int launcher_backup_list( const char *runtime_dir, struct launcher_backup *backups, int max, int *count )
{
    char root[BACKUP_PATH], directory[BACKUP_PATH], manifest[BACKUP_PATH];
    struct launcher_backup found;
    struct dirent *entry;
    DIR *dir;
    int i;

    *count = 0;
    if (max <= 0 || !backups_root( runtime_dir, root, sizeof(root) ) || !(dir = opendir( root ))) return 1;
    while (*count < max && (entry = readdir( dir )))
    {
        struct manifest_total total;
        unsigned int files;

        if (!backup_name_valid( entry->d_name )) continue;
        if (!backup_folder( runtime_dir, entry->d_name, directory, sizeof(directory) ) ||
            !join_path( manifest, sizeof(manifest), directory, "manifest.txt" )) continue;
        total.directory = directory;
        total.bytes = 0;
        if (!read_manifest( manifest, sum_file, &total, &files )) continue;
        memset( &found, 0, sizeof(found) );
        snprintf( found.name, sizeof(found.name), "%.31s", entry->d_name );
        found.files = files;
        found.bytes = total.bytes;
        backups[(*count)++] = found;
    }
    closedir( dir );

    /* Newest first: the names are times, so they sort as they were taken. */
    for (i = 1; i < *count; i++)
    {
        struct launcher_backup moving = backups[i];
        int j = i;

        while (j > 0 && strcmp( backups[j - 1].name, moving.name ) < 0)
        {
            backups[j] = backups[j - 1];
            j--;
        }
        backups[j] = moving;
    }
    return 1;
}

int launcher_backup_count( const char *runtime_dir )
{
    char root[BACKUP_PATH], directory[BACKUP_PATH], manifest[BACKUP_PATH];
    struct dirent *entry;
    DIR *dir;
    int count = 0;

    if (!backups_root( runtime_dir, root, sizeof(root) ) || !(dir = opendir( root ))) return 0;
    while ((entry = readdir( dir )))
    {
        if (!backup_name_valid( entry->d_name )) continue;
        if (!backup_folder( runtime_dir, entry->d_name, directory, sizeof(directory) ) ||
            !join_path( manifest, sizeof(manifest), directory, "manifest.txt" ) ||
            access( manifest, F_OK )) continue;
        count++;
    }
    closedir( dir );
    return count;
}

int launcher_backup_restore( const char *runtime_dir, const char *name,
                             unsigned int *restored, char *error, size_t error_size )
{
    char directory[BACKUP_PATH], manifest[BACKUP_PATH];
    struct restore_state state;
    unsigned int files;

    if (restored) *restored = 0;
    if (error && error_size) error[0] = 0;
    if (!backup_name_valid( name )) return set_error( error, error_size, "Not a backup: %s", name );
    if (!backup_folder( runtime_dir, name, directory, sizeof(directory) ) ||
        !join_path( manifest, sizeof(manifest), directory, "manifest.txt" ))
        return set_error( error, error_size, "Too long a path: %s", name );
    state.directory = directory;
    state.restored = 0;
    state.error = error;
    state.error_size = error_size;
    if (!read_manifest( manifest, put_file_back, &state, &files ))
    {
        if (!error || !error_size) return 0;
        if (!error[0]) set_error( error, error_size, "Could not read the backup %s", name );
        return 0;
    }
    if (restored) *restored = state.restored;
    return 1;
}
