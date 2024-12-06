#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#define DEFAULT_PATH "bin (cópia)"
#define SEARCH_TEXT "perna"
#define NUM_WORKERS 10
int qualquercoisa = 0;

// Thread state enum
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

// Worker thread data structure
typedef struct {
    int id;
    char *file_path;
    RankVar *rank_data;
    ThreadState state;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} WorkerThread;

// Global variables
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

// Worker thread function
void* worker_thread(void *arg) {
    WorkerThread *worker = (WorkerThread*)arg;

    while (!stop_monitoring) {
        pthread_mutex_lock(&worker->mutex);
        
        // Wait while the thread is idle and not told to exit
        while (worker->state == THREAD_IDLE && !stop_monitoring) {
            pthread_cond_wait(&worker->cond, &worker->mutex);
        }

        // Check if we should exit
        if (stop_monitoring) {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        // If we have a file to process
        if (worker->state == THREAD_WORKING && worker->file_path) {
            // Process file
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

                // Safely update count
                pthread_mutex_lock(&rank_mutex);
                if (worker->rank_data) {
                    worker->rank_data->countocur = count;
                }
                
                // Increment completed threads count
                completed_threads++;
                pthread_mutex_unlock(&rank_mutex);

                fclose(p);
            }

            // Free the file path
            free(worker->file_path);
            worker->file_path = NULL;

            // Set thread back to idle state
            worker->state = THREAD_IDLE;
        }

        pthread_mutex_unlock(&worker->mutex);
    }

    return NULL;
}

// Ranker function to sort the files based on occurrences of the search text
void* ranker(void *arg) {
    while (!stop_monitoring) {
        pthread_mutex_lock(&id_mutex);

        int *file_count = (int *)arg;
        if(*file_count == 0){
            printf("Arquivo vazio");
        }
        else{
            for (int i = 0; i < *file_count; i++) {
                for (int j = i + 1; j < *file_count; j++) {
                    if (rank_data[i].countocur < rank_data[j].countocur) {
                        RankVar temp = rank_data[i];
                        rank_data[i] = rank_data[j];
                        rank_data[j] = temp;
                    }
                }
            }

            // Print ranked files
            printf("\nRanked Files:\n");
            for (int i = 0; i < *file_count; i++) {
                if (rank_data[i].file_path) {
                    printf("Position %d: %s, Count: %d\n", 
                        i + 1, 
                        rank_data[i].file_path, 
                        rank_data[i].countocur);
                }
            }
            printf("AQUI %d", qualquercoisa);
            qualquercoisa++;
             // Re-sort and print ranked files every 5 seconds
        }
        pthread_mutex_unlock(&id_mutex);
        sleep(5); 
    }

    return NULL;
}

// Function to assign a file to an idle worker
int assign_file_to_worker(const char *file_path, RankVar *rank_data) {
    pthread_mutex_lock(&queue_mutex);

    // Find an idle worker
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_mutex_lock(&workers[i].mutex);
        
        if (workers[i].state == THREAD_IDLE) {
            // Assign file to this worker
            workers[i].file_path = strdup(file_path);
            workers[i].rank_data = rank_data;
            workers[i].state = THREAD_WORKING;
            
            // Signal the worker thread
            pthread_cond_signal(&workers[i].cond);
            
            pthread_mutex_unlock(&workers[i].mutex);
            pthread_mutex_unlock(&queue_mutex);
            return 1;
        }
        
        pthread_mutex_unlock(&workers[i].mutex);
    }

    // No idle workers, add to queue
    file_queue = realloc(file_queue, (queue_size + 1) * sizeof(char*));
    if (file_queue == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    file_queue[queue_size++] = strdup(file_path);

    pthread_mutex_unlock(&queue_mutex);
    return 0;
}

// Monitor directory for new or modified files
void monitor_directory(const char *path, const char *text) {
    pthread_t thread_ids[NUM_WORKERS];
    pthread_t ranker_thread;

    // Initialize worker threads
    for (int i = 0; i < NUM_WORKERS; i++) {
        workers[i].id = i;
        workers[i].state = THREAD_IDLE;
        workers[i].file_path = NULL;
        pthread_mutex_init(&workers[i].mutex, NULL);
        pthread_cond_init(&workers[i].cond, NULL);
        pthread_create(&thread_ids[i], NULL, worker_thread, &workers[i]);
    }

    // Create the ranker thread
    pthread_create(&ranker_thread, NULL, ranker, (void*)&total_files_to_process);

    int file_count = 0;
    while (!stop_monitoring) {
        DIR *dirp = opendir(path);
        if (dirp == NULL) {
            perror("Failed to open directory");
            break;
        }

        // Count files in the directory
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

        // Reallocate rank_data and reset it if the file count changes
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

        // Reset counters for completed threads and files to process
        completed_threads = 0;
        total_files_to_process = 0;

        // Process each file in the directory
        int count = 0;
        while ((entry = readdir(dirp)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

            if (stat(full_path, &path_stat) == 0) {
                // If the file has been modified
                rank_data[count].file_path = strdup(full_path);
                rank_data[count].countocur = 0;
                
                // Assign file to a worker
                assign_file_to_worker(full_path, &rank_data[count]);
                total_files_to_process++;
                count++;
            }
        }
        closedir(dirp);

        // Wait for all threads to complete processing
        while (completed_threads < total_files_to_process) {
            usleep(100000);  // Short sleep to prevent busy waiting
        }

        sleep(5);  // Check the directory every 5 seconds
    }

    // Cleanup
    stop_monitoring = 1;
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(thread_ids[i], NULL);
        pthread_mutex_destroy(&workers[i].mutex);
        pthread_cond_destroy(&workers[i].cond);
    }

    // Wait for the ranker thread to finish
    pthread_join(ranker_thread, NULL);

    // Free allocated memory
    for (int i = 0; i < file_count; i++) {
        free(rank_data[i].file_path);
    }
    free(rank_data);

    // Free file queue
    for (int i = 0; i < queue_size; i++) {
        free(file_queue[i]);
    }
    free(file_queue);
}

int main() {
    monitor_directory(DEFAULT_PATH, SEARCH_TEXT);
    return 0;
}
