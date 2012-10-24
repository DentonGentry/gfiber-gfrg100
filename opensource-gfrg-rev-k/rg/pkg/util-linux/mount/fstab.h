#define _PATH_FSTAB	"/etc/fstab"
#ifdef _PATH_MOUNTED
#define MOUNTED_LOCK	_PATH_MOUNTED "~"
#define MOUNTED_TEMP	_PATH_MOUNTED ".tmp"
#else
#define MOUNTED_LOCK	"/etc/mtab~"
#define MOUNTED_TEMP	"/etc/mtab.tmp"
#endif
#define LOCK_TIMEOUT	10

int mtab_is_writable(void);
int mtab_does_not_exist(void);
int mtab_is_a_symlink(void);

struct mntentchn {
     struct mntentchn *nxt, *prev;
     char *mnt_fsname;
     char *mnt_dir;
     char *mnt_type;
     char *mnt_opts;
};

struct mntentchn *mtab_head (void);
struct mntentchn *getmntfile (const char *name);
struct mntentchn *getmntoptfile (const char *file);
struct mntentchn *getmntfilesbackward (const char *file, struct mntentchn *mc);

struct mntentchn *fstab_head (void);
struct mntentchn *getfsfile (const char *file);
struct mntentchn *getfsspec (const char *spec);
struct mntentchn *getfsspecfile (const char *spec, const char *file);
struct mntentchn *getfsuuidspec (const char *uuid);
struct mntentchn *getfsvolspec (const char *label);

#include <mntent.h>
void lock_mtab (void);
void unlock_mtab (void);
void update_mtab (const char *special, struct mntent *with);
