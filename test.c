#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <limits.h> 

int count_files(const char *path) {
    int count = 0;
    DIR *dirp = opendir(path);
    if (dirp == NULL) {
        perror("Failed to open directory");
        return -1;
    }
    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(dirp);
    return count;
}

void monitor_directory(const char *path) {
    int size = count_files(path);
    if (size < 0) {
        return; 
    }
    time_t *vat = malloc(size * sizeof(time_t)); 
    if (vat == NULL) {
        perror("Failed to allocate memory");
        return;
    }
    memset(vat, 0, size * sizeof(time_t)); 

    while (1) {
        int size1 = count_files(path);
        if (size1 < 0) {
            break; 
        }
        if (size1 != size) {
            printf("File count changed!\n");
            size = size1;
            vat = realloc(vat, size * sizeof(time_t)); 
            if (vat == NULL) {
                perror("Failed to reallocate memory");
                break;
            }
            memset(vat, 0, size * sizeof(time_t)); 
        }

        DIR *dirp = opendir(path);
        if (dirp == NULL) {
            perror("Failed to open directory");
            break;
        }

        struct dirent *entry;
        int count = 0;
        while ((entry = readdir(dirp)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

            struct stat path_stat;
            if (stat(full_path, &path_stat) == 0) {
                if (count >= size) {
                    printf("Unexpected new file, resizing array.\n");
                    size++;
                    vat = realloc(vat, size * sizeof(time_t));
                    if (vat == NULL) {
                        perror("Failed to reallocate memory");
                        closedir(dirp);
                        return;
                    }
                }

                if (vat[count] != path_stat.st_mtime) {
                    printf("Arquivo '%s' modificado.\n", entry->d_name);
                    //aqui entraria uma thread operaria eu imagino
                    vat[count] = path_stat.st_mtime;
                } else {
                    printf("'%s' Dboa .\n", entry->d_name);
                }
                count++;
            } else {
                perror("Failed to stat file");
            }
        }
        closedir(dirp);
        sleep(5);
    }

    free(vat);
}

int main() {
    monitor_directory("bin");
    return 0;
}
