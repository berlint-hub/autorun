#include <assert.h>
#include "../source/autorun_install.c"

static int fail_publish, fail_restore;
/* A file that is open, as the running runtime's symbol file is: Horizon
 * neither deletes it nor puts another file in its place. */
static const char *held;

static int ends_with(const char *path, const char *suffix)
{
    size_t length = strlen(path), size = strlen(suffix);
    return length >= size && !strcmp(path + length - size, suffix);
}

static int is_held(const char *path)
{
    return held && ends_with(path, held);
}

int __real_rename(const char *from, const char *to);
int __wrap_rename(const char *from, const char *to)
{
    if (fail_publish && strstr(to, "/drive_c/windows/system32/ntdll.dll"))
    { fail_publish = 0; errno = EIO; return -1; }
    if (is_held(from) || is_held(to)) { errno = EBUSY; return -1; }
    /* Undoing the update stops once, as a card can, at a file it had
     * replaced: the next try finishes it. */
    if (fail_restore && ends_with(from, "/restore.part") && ends_with(to, "/wine-nx-runtime-nxvk.nro"))
    { fail_restore = 0; errno = EIO; return -1; }
    return __real_rename(from, to);
}

int __real_unlink(const char *path);
int __wrap_unlink(const char *path)
{
    if (is_held(path)) { errno = EBUSY; return -1; }
    return __real_unlink(path);
}

static void test_managed(void)
{
    const char *replace[] = {
        "wine-nx-runtime.nro", "build-manifest.json", "drive_c/dxvk64/dxgi.dll",
        "drive_c/windows/system32/ntdll.dll", "drive_c/windows/syswow64/kernel32.dll",
        "share/wine/nls/locale.nls", "share/wine/fonts/tahoma.ttf",
        "licenses/Wine-LGPL.txt", "licenses/nxvk/COPYING", "wine-nx-runtime-nxvk.nro",
        "wine-nx-runtime.nro.sym", "wine-nx-runtime-nxvk.nro.sym", "README.txt", "game-fixes/bundled.json",
        "drive_c/windows/autorun-setup.exe"
    };
    const char *keep[] = {
        "config/launcher.txt", "registry/user.reg", "drive_c/users/Switch/save.dat",
        "drive_c/windows/custom.ini", "drive_c/windows/system32/ntdll.dll/child",
        "../wine-nx-runtime.nro", "drive_c/windows/system32/../ntdll.dll",
        "config/settings.json", "config/keys.txt", "game-fixes/index.json", "licenses/../config/keys.txt",
        "wine-nx-runtime-other.nro", "wine-nx-runtime.sym", "wine-nx-runtime-nxvk.sym"
    };
    for (unsigned i = 0; i < sizeof(replace) / sizeof(*replace); i++) assert(managed(replace[i]));
    for (unsigned i = 0; i < sizeof(keep) / sizeof(*keep); i++) assert(!managed(keep[i]));
    puts("app updater: managed paths and state preservation passed");
}

static void test_plan_limits(void)
{
    struct install_plan *plan = calloc(1, sizeof(*plan));
    assert(plan);
    plan->header.magic = INSTALL_MAGIC;
    strcpy(plan->header.tag, "test");
    plan->header.count = 8;
    for (unsigned i = 0; i < 8; i++)
    {
        snprintf(plan->entries[i].path, sizeof(plan->entries[i].path), "drive_c/windows/system32/test%u.dll", i);
        plan->entries[i].size = 512ull * 1024 * 1024;
    }
    strcpy(plan->entries[7].path, INSTALL_NRO);
    assert(valid_plan(plan));
    plan->entries[0].size++;
    assert(!valid_plan(plan));
    plan->entries[0].size--;
    strcpy(plan->entries[7].path, "drive_c/windows/system32/test7.dll");
    strcpy(plan->entries[8].path, INSTALL_NRO);
    plan->entries[8].size = 1;
    plan->header.count = 9;
    assert(!valid_plan(plan));
    plan->header.count = INSTALL_MAX_FILES + 1;
    assert(!valid_plan(plan));
    free(plan);
    puts("app updater: 4GiB plan accepted; aggregate, 512MiB file and 4096-file bounds enforced");
}

int main(int argc, char **argv)
{
    test_managed();
    test_plan_limits();
    if (argc == 1) return 0;
    assert(argc == 4);
    /* 2: an archive the installer must refuse, leaving the card as it was. */
    if (atoi(argv[3]) == 2)
    {
        assert(autorun_install_archive(argv[1], argv[2], "bundle-test", 1, NULL, NULL) == AUTORUN_INSTALL_INVALID);
        assert(autorun_install_recover(argv[1], 0) == 0);
        return 0;
    }
    /* 3: a file the update replaces is open, as the running runtime's symbol
     * file would be had the launcher not closed it (and as a runtime until
     * 22524fc kept wine-nx-runtime.sym): the update fails and is undone
     * around it. 4: undoing it also stops once; the next start's recovery
     * finishes it, with the file still open. */
    if (atoi(argv[3]) == 3 || atoi(argv[3]) == 4)
    {
        held = "/wine-nx-runtime.nro.sym";
        fail_restore = atoi(argv[3]) == 4;
        enum autorun_install_result result = autorun_install_archive(argv[1], argv[2], "bundle-test", 1, NULL, NULL);
        if (atoi(argv[3]) == 4)
        {
            assert(result == AUTORUN_INSTALL_RECOVERY);
            assert(autorun_install_recover(argv[1], 0) == 2);
        }
        else assert(result == AUTORUN_INSTALL_IO);
        assert(autorun_install_recover(argv[1], 0) == 0);
        return 0;
    }
    /* 5: the same open file, which the archive does not bring (it brings
     * wine-nx-runtime.nro.sym): the update goes ahead. */
    if (atoi(argv[3]) == 5) held = "/wine-nx-runtime.sym";
    else fail_publish = atoi(argv[3]);
    enum autorun_install_result result = autorun_install_archive(argv[1], argv[2], "bundle-test", 1, NULL, NULL);
    if (atoi(argv[3]) == 1)
    {
        assert(result == AUTORUN_INSTALL_IO);
        assert(autorun_install_recover(argv[1], 0) == 0);
    }
    else
    {
        if (result != AUTORUN_INSTALL_OK) fprintf(stderr, "%s\n", autorun_install_error(result));
        assert(result == AUTORUN_INSTALL_OK);
        assert(autorun_install_recover(argv[1], 0) == 1);
        /* The first start after the update is told so (the launcher then
         * brings the DLLs); the next one finds nothing to finish. */
        assert(autorun_install_finish(argv[1]) == 2);
        assert(autorun_install_finish(argv[1]) == 1);
    }
    return 0;
}
