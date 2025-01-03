 ***Paralegrep***

Alunos: Ciro Moraes, Giulianna Ellen e Hugo Souza

Visão Geral: 
O projeto em linguagem C consiste em ser  uma ferramenta de busca de arquivos multithread que verifica um diretório especificado (`bin` por padrão, altere no código para analisar uma pasta diferente) para arquivos que contenham uma determinada sequência de pesquisa. Ele utiliza 3 threads para processar arquivos simultaneamente, sendo essas as threads de monitoramento, as operárias(10) e a thread ranking que os classifica com base na contagem de ocorrências da sequência de pesquisa. O programa ajusta dinamicamente o número de arquivos sendo processados, exibe os arquivos classificados e manipula as alterações de diretório durante o tempo de execução.

Principais recursos: 
- Multithreading com threads de trabalho para processar vários arquivos simultaneamente. 
- Contagem de ocorrências de sequência de pesquisa em cada arquivo.
- Gerenciamento dinâmico de fila de arquivos para equilibrar a carga de trabalho entre os threads.
- Classificação periódica de arquivos com base nas ocorrências de pesquisa. 
- Monitoramento de diretório para novos arquivos.

Requisitos :
 - Um ambiente Linux (ou qualquer ambiente com suporte POSIX).
 - GCC ou outro compilador C. 
 - Biblioteca pthread para suporte multithreading.

Compilação :
- Para compilar o programa, use o seguinte comando: 

    gcc  projso.c -o paralegrep -lpthread 

Uso:
 O programa recebe um argumento de linha de comando: 

   ./paralegrep <palavra> 

Onde  <palavra>  é o texto a ser pesquisado nos arquivos localizados no diretório `bin` (diretório padrão). O programa verificará todos os arquivos neste diretório e contará as ocorrências da string fornecida. Os arquivos são então classificados com base na contagem de ocorrências. 

     Exemplo:

      bash
       ./paralegrep caixa 

       Isso pesquisará a string `"caixa"` em todos os arquivos dentro do diretório `bin` e classificará os arquivos com base no número de ocorrências.

Como funciona :
     1. Monitoramento de diretório:
    O programa monitora constantemente o diretório `bin` para alterações (por exemplo, novos arquivos adicionados, arquivos existentes removidos). Ele reprocessa os arquivos sempre que o número de arquivos no diretório muda. 
     2. Worker Threads:
      O programa gera um conjunto de worker threads (`NUM_WORKERS` definido como 10 por padrão). Cada worker thread é responsável por pesquisar em um arquivo por ocorrências da string de pesquisa. Quando um worker termina de processar um arquivo, ele atualiza uma estrutura de dados compartilhada com a contagem de ocorrências.
     3. Ranking: 
     Um thread "ranker" separado classifica periodicamente os arquivos com base no número de ocorrências da string de pesquisa, imprimindo os arquivos mais bem classificados.
     4. File Assignment:
      Se um worker thread estiver ocioso, um arquivo será atribuído a ele. Se todos os workers estiverem ocupados, o arquivo será adicionado a uma fila para processamento posterior. O programa aguarda a conclusão de todos os threads antes de exibir os resultados.
      
Estruturas de dados :
  -WorkerThread: Representa um worker thread. Cada worker tem um `id`, um ponteiro para o arquivo que está processando, os dados `RankVar` associados (contagem de ocorrências) e variáveis ​​de sincronização para mutexes e variáveis ​​de condição. 
  - RankVar:
  Armazena o caminho para o arquivo e a contagem de ocorrências da string de pesquisa nesse arquivo.

Variáveis Globais:
  - workers[]: Um array contendo as threads de trabalho.
  - file_queue[]: Uma fila para armazenar os caminhos dos arquivos que não puderam ser imediatamente atribuídos a uma thread.
  - rank_data[]: Um array que armazena os dados de classificação de cada arquivo processado.

Sincronização:

  O programa utiliza diversos mutexes e variáveis de condição para garantir que as threads não acessem recursos compartilhados (como caminhos de arquivos e dados de classificação) simultaneamente, evitando condições de corrida:
  - queue_mutex: Garante a exclusão mútua ao acessar a fila de arquivos.
  - rank_mutex: Garante a exclusão mútua ao atualizar os dados de classificação.
  - id_mutex: Garante a exclusão mútua ao atualizar o estado da thread ou verificar a conclusão.

Macros Importantes:

  - DEFAULT_PATH: O diretório padrão onde o programa busca os arquivos. Por padrão, é definido como "bin".
  - NUM_WORKERS: O número de threads de trabalho criadas. Este valor é definido como 10, mas pode ser modificado.

Gerenciamento de Memória:

  A memória para os caminhos dos arquivos, threads de trabalho e dados de classificação é alocada dinamicamente durante a execução e liberada quando o programa termina. O programa também lida com a duplicação de caminhos de arquivos e sincronização de forma apropriada para evitar vazamentos de memória e erros.

