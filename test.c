#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <limits.h> 
#include <pthread.h>

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

void count_ocur(const char *path, const char *text){
    FILE *p = fopen(path, "r");
    char str[1000];
    char *pos;
    int i, count = 0;
    
    while((fgets(str, 1000, p)) != NULL){
        i = 0;
        while ((pos = strstr(str+i, text)) != NULL){
            i = (pos - str) + 1;
            count++;
        
        }
    }
    printf("%d ocurrences found in %s", count, path);


}
void monitor_directory(const char *path, const char *text) {
    int size = count_files(path);
    pthread_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9;
    pthread_t fila[10];

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
            //aqui também entraria ação das threads operárias
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
                    //count_ocur(full_path, text);
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

int main(int argc, char* argv[]) {
    if (argc > 1) { 
        char *s = argv[1]; 
        monitor_directory("bin", s);
    }
    
    return 0;
}
