#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <ftw.h>
#include <pwd.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct mount {
  char *source;
  char *type;
  char *target;
  unsigned long flags;
  bool mounted;
};

static struct {
  uid_t uid;
  gid_t gid;
  char *home;
  char *name;
  char *shell;
} user;

static struct {
  char *root;
  char *home;
} path;

/**
 * mounts stores all mounts performed inside the chroot.
 */
static struct mount mounts[] = {
  {"/bin"},
  {"/dev"},
  {"/dev/pts", "pts"},
  {"/etc"},
  {"/lib"},
  {"/lib64"},
  {"/proc", "proc"},
  {"/sys", "sysfs"},
  {"/tmp"},
  {"/usr"},
  {"/var"},
};

static struct mount home;

/**
 * mkdirv create a directory and prints possible error warnings besides EEXIST
 * to stderr.
 */
static void
mkdirv (const char *path, mode_t mode)
{
  errno = 0;
  if (mkdir (path, mode) != 0) {
    if (errno != EEXIST)
      warn ("mkdir");
  }
}

/**
 * mkdirp creates all parent directories as needed.
 */
static void
mkdirp (const char *path)
{
  const mode_t mode = 0777;
  char *copy = NULL;
  char *c;

  copy = strdup (path);
  if (copy == NULL)
    err (EXIT_FAILURE, "strdup");

  for (c = copy; *c != '\0'; c++) {
    if (*c == '/')
      *c = '\0';
  }

  if (*copy)
    mkdirv (copy, mode);

  for (c = copy; *path != '\0'; path++, c++) {
    if (*path != '/')
      continue;
    *c = '/';
    mkdirv (copy, mode);
  }
  free (copy);
}

/**
 * exists returns true if path exists.
 */
static int
exists (const char *path)
{
  struct stat buf;
  return (stat (path, &buf) == 0);
}

/**
 * mount_bind attaches the filesystem specified by src to the directory specified
 * by dst. If dst is NULL, src will be used as target directory in the root directory
 * of the box.
 */
static void
mount_bind (struct mount *mnt)
{
  const unsigned long default_flags = MS_BIND | MS_RDONLY;

  if (!exists (mnt->source))
    return;

  if (mnt->target == NULL) {
    if (asprintf (&mnt->target, "%s%s", path.root, mnt->source) < 0)
      err (EXIT_FAILURE, "asprintf");
  }

  if (mnt->type == NULL)
    mnt->type = "";

  mkdirp (mnt->target);
  if (mount (mnt->source, mnt->target, mnt->type, default_flags & ~mnt->flags, NULL) != 0)
    err (EXIT_FAILURE, "mount");
  mnt->mounted = true;
}

/**
 * mount_eject ejects a mount point.
 */
static void
mount_eject (struct mount *mnt)
{
  if (mnt->mounted) {
    if (umount (mnt->target) != 0)
      warn ("umount");
    mnt->mounted = false;
  }
}

static int
walk_check (const char *path, const struct stat *sb, int type, struct FTW *buf)
{
  return (buf->level > 2) || (type != FTW_D);
}

static int
walk_remove (const char *path, const struct stat *sb, int type, struct FTW *buf)
{
  if ((FTW_D == type) || (FTW_DP == type)) {
    if (rmdir (path) != 0)
      warn ("rmdir");
  }
  return 0;
}

/**
 * cleanup removes a directory if it contains empty directories only and no
 * subtree exceeds depth 2. This function is used to remove the box' root directory
 * at exit. If anything unexpectedly changed, it aborts and removes nothing.
 */
static int
cleanup (const char *path)
{
  if (nftw (path, walk_check, 8, 0) != 0)
    return -1;
  return nftw (path, walk_remove, 8, FTW_DEPTH);
}

#define lengthof(x) (sizeof (x) / sizeof ((x)[0]))

int
main (int argc, char **argv)
{
  struct passwd *pwd;

  /**
   * We want per-process mounts.
   */
  if (unshare (CLONE_NEWNS) != 0)
    err (EXIT_FAILURE, "unshare");

  /**
   * Make sure mounts are visible in this namespace only. Relevant on systems
   * running systemd's sandbox script.
   */
  if (mount (NULL, "/", "", MS_PRIVATE | MS_REC, NULL) != 0)
    err (EXIT_FAILURE, "mount");

  user.gid = getgid ();
  user.uid = getuid ();
  if (user.uid == 0)
    errx (EXIT_FAILURE, "root");

  pwd = getpwuid (user.uid);
  if (pwd == NULL)
    err (EXIT_FAILURE, "getpwuid");

  user.home = pwd->pw_dir;
  user.name = pwd->pw_name;
  user.shell = pwd->pw_shell;

  /**
   * Use "$HOME/.cache/dotbox/$PID" as the box' root directory.
   */
  if (asprintf (&path.root, "%s/.cache/dotbox/%d", user.home, getpid ()) < 0)
    err (EXIT_FAILURE, "asprintf");

  if (asprintf (&path.home, "%s%s", path.root, user.home) < 0)
    err (EXIT_FAILURE, "asprintf");

  if (exists (path.root))
    errx (EXIT_FAILURE, "%s exists", path.root);

  mkdirp (path.root);
  mkdirp (path.home);

  int i;
  for (i = 0; i < (int) lengthof (mounts); i++)
    mount_bind (mounts + i);

  if (argc > 1) {
    home.source = argv[1];
    home.target = path.home;
    mount_bind (&home);
  }

  int status = 0;
  pid_t pid;
  pid_t ret;

  pid = fork ();
  if (pid == -1)
    err (EXIT_FAILURE, "fork");

  if (pid == 0) {
    if (chroot (path.root) != 0)
      err (EXIT_FAILURE, "chroot");

    if (chdir (user.home) != 0)
      err (EXIT_FAILURE, "chdir");

    if (setgid (user.gid) != 0)
      err (EXIT_FAILURE, "setgid");

    if (setuid (user.uid) != 0)
      err (EXIT_FAILURE, "setuid");

    if (setuid (0) == 0)
      err (EXIT_FAILURE, "permissions restorable");

    if (execl (user.shell, user.shell, NULL) != 0)
      err (EXIT_FAILURE, "execl");
  }
  else {
    for (;;) {
      errno = 0;
      ret = wait (&status);
      if ((ret >= 0) || ((ret == -1) && (errno == EINTR)))
        continue;
      if (errno != ECHILD)
        warn ("wait");
      break;
    }
  }

  mount_eject (&home);
  for (i = lengthof (mounts) - 1; i >= 0; i--)
    mount_eject (mounts + i);

  if (cleanup (path.root) != 0)
    warnx ("cleanup: leaving %s untouched", path.root);

  return WEXITSTATUS (status);
}
