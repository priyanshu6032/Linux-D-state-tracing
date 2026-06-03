#define FUSE_USE_VERSION 31
#define EXIT_STRING "EXIT\n"

#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pthread.h>
#include <lttng/tracef.h>
#include <assert.h>
#include <signal.h>

struct files {
    char path[50];
    mode_t mode;
    int links;
    int size;
    char data[1024];
};

typedef struct files files;

files base[10];
int inside = 0;

pthread_mutex_t lock;

__attribute__((noinline)) static void run_child(void)
{
    char input[sizeof(EXIT_STRING)];

    while (1) {
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (ferror(stdin)) {
                fprintf(stderr,
                        "Error reading stdin in child\n");
            }
            if (feof(stdin)) {
                fprintf(stderr,
                        "EOF waiting for exit string\n");
            }
            break;
        }
        if (strcmp(input, EXIT_STRING) == 0) {
            break;
        }
        if (strlen(input) == sizeof(input) - 1 &&
            input[strlen(input) - 1] != '\n') {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }
    }

    _exit(0);
}

void init_fs() {
    strcpy(base[0].path, "/");
    base[0].mode = S_IFDIR | 0755;
    base[0].links = 2;

    inside = 1;
}

void insertDir(int directories){
    base[directories].mode = S_IFDIR | 0755;
    base[directories].links = 2;
}

void insertFile(int directories,mode_t mode){
    base[directories].mode = S_IFREG | (mode & 0777); 
    base[directories].links = 1;
    base[directories].size = 0;
    memset(base[directories].data, 0, sizeof(base[directories].data));
}
static int do_getattr(const char *path, struct stat *st)
{
    printf("[getattr] %s\n", path);

    memset(st, 0, sizeof(struct stat));

    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time(NULL);
    st->st_mtime = time(NULL);

    if (strcmp(path, base[0].path) == 0) {
        st->st_mode = base[0].mode;
        st->st_nlink = base[0].links;
        return 0;
    }
    for (int i = 1; i < inside; i++) {
        if (strcmp(path, base[i].path) == 0) {
            st->st_mode = base[i].mode;
            st->st_nlink = base[i].links;
            st->st_size = base[i].size;
            return 0;
	}
    }
    return -ENOENT;
}
static int do_create(const char *path, mode_t mode, struct fuse_file_info *fi){
strcpy(base[inside].path,path);
insertFile(inside,mode);
inside++;
return 0;
}

static int do_readdir(const char *path, void *buffer,
                     fuse_fill_dir_t filler, off_t offset,
                     struct fuse_file_info *fi)
{
    printf("[readdir] %s\n", path);

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buffer, ".", NULL, 0, 0);
    filler(buffer, "..", NULL, 0, 0);
    for(int i =1;i<inside;i++){
			filler(buffer,base[i].path+1,NULL,0,0);
		}

    return 0;
}

static int do_mkdir(const char *path , mode_t mode){
	(void) mode;
	printf("Creating directory: %s\n", path);
	strcpy(base[inside].path, path);
    insertDir(inside);
    inside++;
	return 0;
}

static int do_utimens(const char *path, const struct timespec tv[2])
{
    printf("[utimens] %s\n", path);
    for (int i = 0; i < inside; i++) {
        if (strcmp(path, base[i].path) == 0) {
            return 0;
        }
    }

    return -ENOENT;
}

static int do_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&lock);
    for(int i = 0; i < inside; i++){
        if(strcmp(path, base[i].path) == 0){
            if (offset + size > 1024){
            pthread_mutex_unlock(&lock);
                return -ENOSPC;}
            memcpy(base[i].data + offset, buf, size);
            if (offset + size > base[i].size) {
                base[i].size = offset + size;
            }
            
            pthread_mutex_unlock(&lock);
            return size;
        }
    }
    pthread_mutex_unlock(&lock);
    return -ENOENT;
}

static int do_read(const char *path, char *buf, size_t size,
                   off_t offset, struct fuse_file_info *fi)
{

    pid_t pid;
    sigset_t set;
    sigfillset(&set);
    assert(sigprocmask(SIG_BLOCK, &set, NULL)==0);

    pid = vfork();

    if (pid == 0){
        run_child();
    } else if (pid > 0){

        pthread_mutex_lock(&lock);

        for (int i = 0; i < inside; i++) {
            if (strcmp(path, base[i].path) == 0) {

                if (offset >= base[i].size) {
                    pthread_mutex_unlock(&lock);
                    return 0;
                }

                if (offset + size > base[i].size) {
                    size = base[i].size - offset;
                }

                memcpy(buf, base[i].data + offset, size);

                pthread_mutex_unlock(&lock);
                return size;
            }
        }

        pthread_mutex_unlock(&lock);
        return -ENOENT;
    }
}

int do_open(const char *path, struct fuse_file_info *fi) {
    for (int i = 0; i < inside; i++) {
        if (strcmp(path, base[i].path) == 0) {
            if ((fi->flags & O_ACCMODE) != O_RDONLY) {
                return -EACCES;
            }

            fi->fh = i;  
            return 0;    
        }
    }

    return -ENOENT;  
}
static struct fuse_operations operations = {
    .getattr = do_getattr,
    .readdir = do_readdir,
    .mkdir = do_mkdir,
    .create = do_create,
    .utimens = do_utimens,
    .write = do_write,
    .read = do_read,
    .open = do_open
};

int main(int argc, char *argv[])
{
    init_fs();
    pthread_mutex_init(&lock, NULL);
    return fuse_main(argc, argv, &operations, NULL);
}