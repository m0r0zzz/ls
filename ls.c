#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h> 

#include <sys/types.h>
#include <sys/stat.h>

/**
 * Program name, for printing out errors
 */
static const char* progn = NULL;

/**
 * To-be listed path definition
 */
struct path_def {
    const char* path; //!< filesystem path
    struct stat stat; //!< entry information from stat(2)
    int is_valid; //!< validity flag, 0 if stat(2) returned error for this path
};


/**
 * Initialize path definition
 * @param self Pointer to path definition to be initialized
 * @param path Filesystem path for this path definition
 */
void path_def_init(struct path_def* self, const char* path){
    int ret;
    self->path = path;
    ret = stat(path, &self->stat);
    if(ret < 0){
	self->is_valid = 0;
	fprintf(stderr, "%s: cannot access '%s': %s", progn, path, strerror(errno));
    } else {
	self->is_valid = 1;
    }
}

/**
 * Convert stat.st_mode to file type character
 * @param mode stat.st_mode
 * @return File type character
 */
char ftype_to_char(mode_t mode){
    switch (mode & S_IFMT) {
    case S_IFIFO: return 'f';
    case S_IFCHR: return 'c';
    case S_IFDIR: return 'd';
    case S_IFBLK: return 'b';
    case S_IFREG: return '-';
    case S_IFLNK: return 'l';
    case S_IFSOCK: return 's';
    default: return '?';
    }
}

/**
 * Print one final directory entry to stdout.
 * @param path Directory entry filesystem path (will be printed as-is)
 * @param stat Directory entry information from stat(2). Optional. If it is
 * not equal to NULL directory entry will be printed in long form (incl. mode
 * string, user, group etc.)
 */
void print_one_dent(const char* path, struct stat* stat){
    //static const char ftype_chars[] = "?fcdb-ls";
    
    if(stat == NULL){
	printf("%s\n", path);
    } else {
	char mode[11] = {};
	struct passwd *user;
	struct group *group;
	const char *username, *grpname;
	char* mtime;
	//fill mode
	mode[0] = ftype_to_char(stat->st_mode);

	mode[1] = (stat->st_mode & S_IRUSR) ? 'r' : '-';
	mode[2] = (stat->st_mode & S_IWUSR) ? 'w' : '-';
	mode[3] = (stat->st_mode & S_IXUSR) ? 'x' : '-';

	mode[4] = (stat->st_mode & S_IRGRP) ? 'r' : '-';
	mode[5] = (stat->st_mode & S_IWGRP) ? 'w' : '-';
	mode[6] = (stat->st_mode & S_IXGRP) ? 'x' : '-';

	mode[7] = (stat->st_mode & S_IROTH) ? 'r' : '-';
	mode[8] = (stat->st_mode & S_IWOTH) ? 'w' : '-';
	mode[9] = (stat->st_mode & S_IXOTH) ? 'x' : '-';

	if(stat->st_mode & S_ISUID) mode[3] = (stat->st_mode & S_IXUSR) ? 's' : 'S';
	if(stat->st_mode & S_ISGID) mode[6] = (stat->st_mode & S_IXGRP) ? 's' : 'S';
	if(stat->st_mode & S_ISVTX) mode[9] = (stat->st_mode & S_IXOTH) ? 't' : 'T';
	mode[10] = '\0';

	user = getpwuid(stat->st_uid);
	username = user ? user->pw_name : NULL;
	group = getgrgid(stat->st_gid);
	grpname = group ? group->gr_name : NULL;
	mtime = ctime(&stat->st_mtim.tv_sec);
	//specialized crutch - remove newline from the end of string
	mtime[strlen(mtime) - 1] = '\0';

	printf("%s\t%lu\t%s\t%s\t%ld\t%s\t%s\n", mode, stat->st_nlink, username, grpname, stat->st_size, mtime, path);
    }
}

/**
 * Prints one path definition (file or directory) to stdout. Enumerates
 * directory entries in case path definition contains directory definition.
 * Errorneous directory entries are ignored.
 * @param self Input path definition.
 * @param is_long Select directory entry output format (-l program option)
 * @param is_show_hidden Select if function should ignore entries with names
starting with '.' (-a program option).
 * @return Error code - 0 in case of no error, -1 in case of one or more errors.
 */
int path_def_print(struct path_def *self, int is_long, int is_show_hidden) {
    int fret = 0;
    if(!self->is_valid) return -1;

    if(S_ISDIR(self->stat.st_mode)){
	int dir_fd;
	struct dirent* dent;
	int ret;
	DIR* dir = opendir(self->path);

	if(dir == NULL){
	    fprintf(stderr, "%s: cannot access '%s' contents: %s", progn, self->path, strerror(errno));
	    return -1;
	}
	dir_fd = dirfd(dir);
	errno = 0;
	//TODO: sort dir entries before printing
	while((dent = readdir(dir)) != NULL){
	    if(!is_show_hidden && dent->d_name[0] == '.') continue; //ignore hidden files
	    
	    if(is_long){
		struct stat stat;
		ret = fstatat(dir_fd, dent->d_name, &stat, AT_SYMLINK_NOFOLLOW);
		if(ret < 0) {
		    fprintf(stderr, "%s: cannot access '%s' at '%s': %s", progn, dent->d_name, self->path, strerror(errno));
		    fret = -1;
		    continue;
		}
		print_one_dent(dent->d_name, &stat);
	    } else {
		print_one_dent(dent->d_name, NULL);
	    }
	}
	
	closedir(dir);
    } else {
	print_one_dent(self->path, &self->stat);
    }
    return fret;
}


/**
 * Main function.
 * @param argc Argument count
 * @param argv Argument vector
 * @return Return code
 */
int main(int argc, char** argv){
    int opt = 0, is_long = 0, is_hidden = 0;
    char** paths = NULL, **path = NULL;
    int paths_num;
    struct path_def* path_defs = NULL;
    int has_errors = 0;
    int i, ret;

    progn = argv[0];
    
    while ((opt = getopt(argc, argv, "lah")) != -1){
	switch (opt) {
	case 'l':
	    is_long = 1;
	    break;
	case 'a':
	    is_hidden = 1;
	    break;
	default:
	    fprintf(stderr, "Usage: %s [-l] [PATH]...\n", argv[0]);
	    exit(EXIT_FAILURE);
	}
    }

    paths = &argv[optind];
    for(paths_num = 0, path = paths; *path; ++paths_num, ++path); 

    if(paths_num == 0){
	paths_num = 1;
	path_defs = malloc(sizeof(struct path_def) * 1);
	path_def_init(path_defs, ".");
    } else {
	path_defs = malloc(sizeof(struct path_def) * paths_num);
	for(i = 0; i < paths_num; ++i){
	    path_def_init(&path_defs[i], paths[i]);
	}
    }

    //print files first
    for(i = 0; i < paths_num; ++i){
	if(path_defs[i].is_valid && !S_ISDIR(path_defs[i].stat.st_mode)){
	    ret = path_def_print(&path_defs[i], is_long, is_hidden);
	    if(ret < 0) has_errors = 1;
	} else if(!path_defs[i].is_valid){
	    has_errors = 1;
	}
    }

    //print dirs next
    for(i = 0; i < paths_num; ++i){
	if(path_defs[i].is_valid && S_ISDIR(path_defs[i].stat.st_mode)){
	    //prefix with empty line if this path is not first
	    if(i > 0){
		puts("\n");
	    }
	    //print path if there is multiple paths
	    if(paths_num > 1){
		printf("%s:\n", path_defs[i].path);
	    }
	    
	    ret = path_def_print(&path_defs[i], is_long, is_hidden);
	    if(ret < 0) has_errors = 1;
	} else if(!path_defs[i].is_valid){
	    has_errors = 1;
	}
    }

    //finalize
    free(path_defs);
    return has_errors ? EXIT_FAILURE : EXIT_SUCCESS;
}
