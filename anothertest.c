#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <limits.h> // For PATH_MAX

int tamanho(char *bin){
    int file_count = 0;
    DIR *dirp;
    struct dirent *entry;

    dirp = opendir(bin);
    
    while ((entry = readdir(dirp)) != NULL) {
        
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "bin/%s", entry->d_name);

        struct stat path_stat;
        if (stat(full_path, &path_stat) == 0) {
            if (S_ISREG(path_stat.st_mode)) {
                file_count++;

            }
        } else {
            perror("Failed to stat file");
        }
    }

    closedir(dirp);
    return file_count;
}
