#include <stdio.h>   // Biblioteca para entrada e saida
#include <stdlib.h>  // Biblioteca para manipulacao de memoria e processos
#include <string.h>  // Biblioteca para manipulacao de strings
#include <pthread.h> // Biblioteca para criacao e manipulacao de threads
#include <dirent.h>  // Biblioteca para manipulacao de diretorios
#include <sys/stat.h> // Biblioteca para obtencao de informacoes sobre arquivos
#include <unistd.h>   // Biblioteca para chamadas ao sistema POSIX
#include <limits.h>   // Biblioteca para definicoes de limites do sistema

// Define o caminho padrao para os arquivos a serem monitorados
define DEFAULT_PATH "bin"
// Define o numero de threads que serao criadas
define NUM_WORKERS 10

// Buffer global para armazenar o texto a ser buscado
char SEARCH_TEXT[1000] = "";

// Define os estados possiveis de uma thread
typedef enum {
    THREAD_IDLE,     // Thread esta ociosa
    THREAD_WORKING,  // Thread esta processando
    THREAD_EXIT      // Thread deve encerrar
} ThreadState;

// Estrutura para armazenar informacoes sobre ocorrencias em um arquivo
typedef struct {
    char *file_path; // Caminho do arquivo
    int countocur;   // Numero de ocorrencias da string de busca
    int file_count;  // Numero total de arquivos
} RankVar;

// Estrutura que representa uma thread de trabalhador
typedef struct {
    int id;                   // ID da thread
    char *file_path;          // Caminho do arquivo associado a thread
    RankVar *rank_data;       // Informacoes de ranking associadas ao arquivo
    ThreadState state;        // Estado atual da thread
    pthread_mutex_t mutex;    // Mutex para sincronizacao
    pthread_cond_t cond;      // Condicao para controle do estado
} WorkerThread;

// Declaracao de variaveis globais
WorkerThread workers[NUM_WORKERS];
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para a fila de arquivos
pthread_mutex_t rank_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex para o ranking
pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;    // Mutex para controle de IDs
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;    // Condicao para controle da fila

char **file_queue = NULL; // Fila de arquivos a serem processados
int queue_size = 0;       // Tamanho da fila
volatile int stop_monitoring = 0; // Flag para encerrar o monitoramento
int completed_threads = 0; // Numero de threads que completaram o trabalho
int total_files_to_process = 0; // Numero total de arquivos a serem processados
RankVar *rank_data = NULL; // Dados de ranking dos arquivos

// Funcao que sera executada por cada thread de trabalhador
void* worker_thread(void *arg) {
    WorkerThread worker = (WorkerThread)arg; // Cast do argumento para a estrutura WorkerThread

    while (!stop_monitoring) {
        pthread_mutex_lock(&worker->mutex); // Bloqueia o mutex da thread
        
        while (worker->state == THREAD_IDLE && !stop_monitoring) {
            pthread_cond_wait(&worker->cond, &worker->mutex); // Aguarda por uma condicao para trabalhar
        }

        if (stop_monitoring) { // Verifica se deve encerrar
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        if (worker->state == THREAD_WORKING && worker->file_path) {
            FILE *p = fopen(worker->file_path, "r"); // Abre o arquivo para leitura
            if (p) {
                char str[1000];
                char *pos;
                int i, count = 0;
                
                while((fgets(str, 1000, p)) != NULL){ // Le o arquivo linha por linha
                    i = 0;
                    while ((pos = strstr(str + i, SEARCH_TEXT)) != NULL){ // Busca a string na linha
                        i = (pos - str) + 1;
                        count++;
                    }
                }

                pthread_mutex_lock(&rank_mutex); // Atualiza o ranking com exclusao mutua
                if (worker->rank_data) {
                    worker->rank_data->countocur = count;
                }
                
                completed_threads++;
                pthread_mutex_unlock(&rank_mutex);

                fclose(p);
            }

            free(worker->file_path); // Libera a memoria alocada para o caminho do arquivo
            worker->file_path = NULL;

            worker->state = THREAD_IDLE; // Define o estado da thread como ociosa
        }

        pthread_mutex_unlock(&worker->mutex); // Desbloqueia o mutex da thread
    }

    return NULL;
}

// Funcao que ordena os arquivos pelo numero de ocorrencias e exibe o ranking
void* ranker(void *arg) {
    while (!stop_monitoring) {
        pthread_mutex_lock(&id_mutex);

        int *file_count = (int *)arg;
        if(*file_count > 0 && completed_threads == total_files_to_process) {
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
            for (int i = 0; i < 10 && i < *file_count; i++) {
                if (strlen(rank_data[i].file_path) > 0 && rank_data[i].countocur >= 0) {
                    printf("Posicao %d: %s, Contagem: %d\n", 
                        i + 1, 
                        rank_data[i].file_path, 
                        rank_data[i].countocur);
                }
            }
            
            completed_threads = 0;
        }
        pthread_mutex_unlock(&id_mutex);
        sleep(5); 
    }

    return NULL;
}

// Funcao para atribuir um arquivo a uma thread disponivel ou adiciona-lo na fila
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

// Funcao principal que monitora o diretorio e distribui os arquivos para processamento
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
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..")
[14:01, 18/12/2024] Giulianna: if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue; // Ignora os diretórios especiais "." e ".."
            }
            current_file_count++; // Incrementa o contador de arquivos no diretório
        }
        rewinddir(dirp); // Reposiciona o ponteiro do diretório para o início

        // Verifica se o número de arquivos no diretório mudou
        if (current_file_count != file_count) {
            rank_data = realloc(rank_data, current_file_count * sizeof(RankVar));
            if (rank_data == NULL) {
                perror("Memory allocation failed"); // Exibe erro caso a alocação de memória falhe
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < current_file_count; i++) {
                rank_data[i].file_path = NULL;
                rank_data[i].countocur = 0; // Inicializa a contagem de ocorrências
                rank_data[i].file_count = current_file_count; // Define o total de arquivos
            }
            file_count = current_file_count; // Atualiza o número total de arquivos
        }

        total_files_to_process = 0; // Zera o número de arquivos a processar para esta iteração

        int count = 0; // Contador auxiliar para arquivos
        while ((entry = readdir(dirp)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue; // Ignora novamente os diretórios "." e ".."
            }

            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name); // Concatena o caminho do diretório com o nome do arquivo

            if (stat(full_path, &path_stat) == 0) {
                rank_data[count].file_path = strdup(full_path); // Armazena o caminho completo do arquivo
                rank_data[count].countocur = 0; // Inicializa a contagem de ocorrências do arquivo
                
                assign_file_to_worker(full_path, &rank_data[count]); // Atribui o arquivo a uma thread disponível
                total_files_to_process++; // Incrementa o total de arquivos a processar
                count++;
            }
        }
        closedir(dirp); // Fecha o diretório após a leitura

        // Aguarda até que todas as threads tenham processado os arquivos
        while (completed_threads < total_files_to_process) {
            usleep(100000); // Espera de 100ms
        }

        sleep(5); // Aguarda 5 segundos antes de verificar novamente o diretório
    }

    stop_monitoring = 1; // Define a flag para encerrar o monitoramento
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_cond_signal(&workers[i].cond); // Sinaliza para as threads pararem
        pthread_join(thread_ids[i], NULL); // Aguarda a finalização de cada thread
        pthread_mutex_destroy(&workers[i].mutex); // Destroi o mutex associado
        pthread_cond_destroy(&workers[i].cond); // Destroi a variável de condição
    }

    pthread_join(ranker_thread, NULL); // Aguarda a finalização da thread responsável pelo ranking

    // Libera a memória alocada para os caminhos de arquivo no ranking
    for (int i = 0; i < file_count; i++) {
        free(rank_data[i].file_path);
    }
    free(rank_data); // Libera a memória alocada para os dados de ranking.

   for (int i = 0; i < queue_size; i++) {
        free(file_queue[i]);
    }
    free(file_queue);
}

// Função principal
int main(int argc, char * argv[]) {
    if(argc == 2) {
        // Copia o texto de busca fornecido pelo usuário para a variável global SEARCH_TEXT.
        strcpy(SEARCH_TEXT, argv[1]);
        monitor_directory(DEFAULT_PATH, argv[1]); // Inicia o monitoramento do diretório especificado.
    } else {
        printf("Número incorreto de argumentos. Use: <programa> <texto_de_busca>\n");
    }
    return 0; // Retorna 0 indicando execução bem-sucedida.
}