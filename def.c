#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
//mude aqui o nome da pasta onde os arquivos estarão
#define DEFAULT_PATH "bin"
#define NUM_WORKERS 10

char SEARCH_TEXT[1000] = "";
typedef enum {
    THREAD_IDLE,
    THREAD_WORKING,
    THREAD_EXIT
} ThreadState;

typedef struct {
    char *file_path;
    int countocur;
    int file_count;
} RankVar;

typedef struct {
    int id;
    char *file_path;
    RankVar *rank_data;
    ThreadState state;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} WorkerThread;

WorkerThread workers[NUM_WORKERS];
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rank_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;


char **file_queue = NULL;
int queue_size = 0;
int stop_monitoring = 0;
int completed_threads = 0;
int total_files_to_process = 0;
RankVar *rank_data = NULL;


//thread operária
void* worker_thread(void *arg) {
    WorkerThread *worker = (WorkerThread*)arg;

    while (!stop_monitoring) {
        pthread_mutex_lock(&worker->mutex);
        
        while (worker->state == THREAD_IDLE && !stop_monitoring) {
            pthread_cond_wait(&worker->cond, &worker->mutex);
        }

        if (stop_monitoring) {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        if (worker->state == THREAD_WORKING && worker->file_path) {
            FILE *p = fopen(worker->file_path, "r");
            if (p) {
                char str[1000];
                char *pos;
                int i, count = 0;
                
                while((fgets(str, 1000, p)) != NULL){
                    i = 0;
                    while ((pos = strstr(str + i, SEARCH_TEXT)) != NULL){
                        i = (pos - str) + 1;
                        count++;
                    }
                }

                pthread_mutex_lock(&rank_mutex);
                if (worker->rank_data) {
                    worker->rank_data->countocur = count;
                }
                
                completed_threads++;
                pthread_mutex_unlock(&rank_mutex);

                fclose(p);
            }

            free(worker->file_path);
            worker->file_path = NULL;

            worker->state = THREAD_IDLE;
        }

        pthread_mutex_unlock(&worker->mutex);
    }

    return NULL;
}
//thread ranking
void* ranker(void *arg) {
    while (!stop_monitoring) {
        pthread_mutex_lock(&id_mutex);

        int *file_count = (int *)arg;
        if(*file_count == 0){
            printf("Arquivo vazio");
        }
        else{
            if (completed_threads == total_files_to_process) {
            for (int i = 0; i < *file_count; i++) {
                for (int j = i + 1; j < *file_count; j++) {
                    if (rank_data[i].countocur < rank_data[j].countocur) {
                        RankVar temp = rank_data[i];
                        rank_data[i] = rank_data[j];
                        rank_data[j] = temp;
                    }
                }
            }

            printf("\nArquivos Ranqueados:\n");
            for (int i = 0; i < *file_count; i++) {
                if (rank_data[i].file_path) {
                    printf("Posicao %d: %s, Contagem: %d\n", 
                        i + 1, 
                        rank_data[i].file_path, 
                        rank_data[i].countocur);
                    }
                }
            }
        }
        pthread_mutex_unlock(&id_mutex);
        sleep(5); 
    }

    return NULL;
}

int assign_file_to_worker(const char *file_path, RankVar *rank_data) {
    pthread_mutex_lock(&queue_mutex);

    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_mutex_lock(&workers[i].mutex);
        
        if (workers[i].state == THREAD_IDLE) {
            workers[i].file_path = strdup(file_path);
            workers[i].rank_data = rank_data;
            workers[i].state = THREAD_WORKING;
            
            pthread_cond_signal(&workers[i].cond);
            
            pthread_mutex_unlock(&workers[i].mutex);
            pthread_mutex_unlock(&queue_mutex);
            return 1;
        }
        
        pthread_mutex_unlock(&workers[i].mutex);
    }

    file_queue = realloc(file_queue, (queue_size + 1) * sizeof(char*));
    if (file_queue == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    file_queue[queue_size++] = strdup(file_path);

    pthread_mutex_unlock(&queue_mutex);
    return 0;
}
//thread despachante
void monitor_directory(const char *path, const char *text) {
    pthread_t thread_ids[NUM_WORKERS];
    pthread_t ranker_thread;

    for (int i = 0; i < NUM_WORKERS; i++) {
        workers[i].id = i;
        workers[i].state = THREAD_IDLE;
        workers[i].file_path = NULL;
        pthread_mutex_init(&workers[i].mutex, NULL);
        pthread_cond_init(&workers[i].cond, NULL);
        pthread_create(&thread_ids[i], NULL, worker_thread, &workers[i]);
    }

    pthread_create(&ranker_thread, NULL, ranker, (void*)&total_files_to_process);

    int file_count = 0;
    while (!stop_monitoring) {
        DIR *dirp = opendir(path);
        if (dirp == NULL) {
            perror("Failed to open directory");
            break;
        }

        struct dirent *entry;
        struct stat path_stat;
        int current_file_count = 0;
        
        while ((entry = readdir(dirp)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            current_file_count++;
        }
        rewinddir(dirp);

        if (current_file_count != file_count) {
            rank_data = realloc(rank_data, current_file_count * sizeof(RankVar));
            if (rank_data == NULL) {
                perror("Memory allocation failed");
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < current_file_count; i++) {
                rank_data[i].file_path = NULL;
                rank_data[i].countocur = 0;
                rank_data[i].file_count = current_file_count;
            }
            file_count = current_file_count;
        }

        completed_threads = 0;
        total_files_to_process = 0;

        int count = 0;
        while ((entry = readdir(dirp)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

            if (stat(full_path, &path_stat) == 0) {
                rank_data[count].file_path = strdup(full_path);
                rank_data[count].countocur = 0;
                
                assign_file_to_worker(full_path, &rank_data[count]);
                total_files_to_process++;
                count++;
            }
        }
        closedir(dirp);

        while (completed_threads < total_files_to_process) {
            usleep(100000);
        }

        sleep(5);
        }

    stop_monitoring = 1;
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(thread_ids[i], NULL);
        pthread_mutex_destroy(&workers[i].mutex);
        pthread_cond_destroy(&workers[i].cond);
    }

    pthread_join(ranker_thread, NULL);

    for (int i = 0; i < file_count; i++) {
        free(rank_data[i].file_path);
    }
    free(rank_data);

    for (int i = 0; i < queue_size; i++) {
        free(file_queue[i]);
    }
    free(file_queue);
}

int main(int argc, char * argv[]) {
    if(argc ==2){
        strcpy(SEARCH_TEXT, argv[1]);
        monitor_directory(DEFAULT_PATH, argv[1]);

    }else{
        printf("too many or too little arguments");
    }
    return 0;
}