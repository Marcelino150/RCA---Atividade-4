#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <arpa/inet.h> 
#include <pthread.h>

/*
 * Servidor TCP
 */

struct mensagem{

	int validade; 
	char usuario[20];
	char mensagem[80];
};

struct arg_struct{

	int ns;
	struct sockaddr_in client;
};

int	nmsgs;
struct mensagem msgs[10];
pthread_mutex_t mutex;

void encerraServidor(){


	printf("\nServidor encerrado.\n");
	exit(0);
}

int cadastraMensagem(int ns, struct sockaddr_in client){

	int full = 0;
	struct mensagem recvmsg;

	if (recv(ns, &recvmsg, sizeof(recvmsg), 0) == -1){
		perror("Recv()");
		exit(7);
	}

	pthread_mutex_lock(&mutex);

	if(nmsgs == 10){
		full = 1;
	}

	if (send(ns, &full, sizeof(full), 0) < 0){
		perror("Send()");
		exit(8);
	}

	for(int i = 0; i < 10; i++){
		if(msgs[i].validade == 0){
			msgs[i] = recvmsg;
			nmsgs = nmsgs + 1;
			printf("Mensagem de %s recebida pela porta %d e cadastrada com usuario %s.\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port),recvmsg.usuario);
			break;
	  	}
	}

	pthread_mutex_unlock(&mutex);
}

int removeMensagem(int ns, struct sockaddr_in client){

	int n = 0;
	char recvbuf[512];

	if (recv(ns, recvbuf, sizeof(recvbuf), 0) == -1){
		perror("Recv()");
		exit(11);
	}
	
	pthread_mutex_lock(&mutex);

	for(int i = 0; i < 10; i++){
		if(msgs[i].validade == 1 && strcmp(msgs[i].usuario,recvbuf) == 0){
			n++;
		}
	}	

	if (send(ns, &n, sizeof(n), 0) < 0){
		perror("Send()");
		exit(12);
	}

  	for(int i = 0; i < 10; i++){
		if(msgs[i].validade == 1 && strcmp(msgs[i].usuario,recvbuf) == 0){
			if (send(ns, &msgs[i], sizeof(msgs[i]), 0) < 0){
				perror("Send()");
				exit(13);
	 		}

			msgs[i].validade = 0;
			nmsgs = nmsgs - 1;
		}
	}

	pthread_mutex_unlock(&mutex);
	
	if(n != 0){
		printf("%d mensagenm do usuario %s foram removidas a pedido de %s pela porta %d.\n",n,recvbuf,inet_ntoa(client.sin_addr),ntohs(client.sin_port));
	}
}

int enviaMensagem(int ns, struct sockaddr_in client){

	pthread_mutex_lock(&mutex);

	if(send(ns, &nmsgs, sizeof(nmsgs), 0) < 0){
		perror("Send()");
		exit(9);
	}	
			
	for(int i = 0; i < 10; i++){
		if(msgs[i].validade != 0){
			if (send(ns, &msgs[i], sizeof(msgs[i]), 0) < 0){
				perror("Send()");
				exit(10);
	 		}
		}
	}

	pthread_mutex_unlock(&mutex);

	printf("Todas as mensagens foram enviadas ao cliente %s pela porta %d.\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));
}

void *atenderCliente(void *args){

	int op;
	int ns = ((struct arg_struct*)args)->ns;
	struct sockaddr_in client = ((struct arg_struct*)args)->client;

	printf("Conexao estabelecida com o IP %s na porta %d.\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));

	while(1){

		/* Recebe a operação do cliente*/
		if (recv((int)ns, &op, sizeof(op), 0) == -1)
		{
		    perror("Recv()");
		    exit(6);
		}

		switch(op){

			/* Cadastra nova mensagem */
			case 1:
				cadastraMensagem(ns,client);
				break;

			/* Envia todos as mensasgens cadastradas */
			case 2:					 
				enviaMensagem(ns,client);
				break;

			/* Remove mensagens cadastrada*/
			case 3:	
				removeMensagem(ns,client);
				break;
		
			/* Aceita a/aguarda pela proxima conexão da fila se o cliente desconectar*/
			case 4:
				printf("Conexao com o IP %s (porta %d) encerrada.\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));

				close(ns);
				exit(0);

				break;
		}
	}
}

int inicializaMemSem(){

	nmsgs = 0;

	for(int i = 0; i < 10; i++){
		msgs[i].validade = 0;
	}
}

int main(int argc, char **argv)
{
    unsigned short port;                       
    struct sockaddr_in client; 
    struct sockaddr_in server; 
    int s;                     /* Socket para aceitar conexoes       */
    int ns;                    /* Socket conectado ao cliente        */
    int namelen;
    int tp;
    pid_t pid, fid;
    pthread_t thread;

    struct arg_struct arguments;

    signal(SIGINT, encerraServidor);
 
    inicializaMemSem();

    /*
     * O primeiro argumento (argv[1]) e a porta
     * onde o servidor aguardara por conexoes
     */
    if (argc != 2)
    {
        fprintf(stderr, "Use: %s porta\n", argv[0]);
        exit(1);
    }

    port = (unsigned short) atoi(argv[1]);

    /*
     * Cria um socket TCP (stream) para aguardar conexoes
     */
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket()");
        exit(2);
    }

   /*
    * Define a qual endereco IP e porta o servidor estara ligado.
    * IP = INADDDR_ANY -> faz com que o servidor se ligue em todos
    * os enderecos IP
    */
    server.sin_family = AF_INET;   
    server.sin_port   = htons(port);       
    server.sin_addr.s_addr = INADDR_ANY;

    /*
     * Liga o servidor a porta definida anteriormente.
     */
    if (bind(s, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
       perror("Bind()");
       exit(3);
    }

    /*
     * Prepara o socket para aguardar por conexoes e
     * cria uma fila de conexoes pendentes.
     */
    if (listen(s, 1) != 0)
    {
        perror("Listen()");
        exit(4);
    }

    /*
     * Aceita uma conexao e cria um novo socket atraves do qual
     * ocorrera a comunicacao com o cliente.
     */

	if(pthread_mutex_init(&mutex,NULL) != 0){
		fprintf(stderr,"Erro ao iniciar o semaforo\n");
		exit(-1);
	}


	while(1){

		namelen = sizeof(client);
		if ((ns = accept(s, (struct sockaddr *)&client, (socklen_t *)&namelen)) == -1)
		{
			perror("Accept()");
			exit(5);
		}
		
		arguments.ns = ns;
		arguments.client = client;

		tp = pthread_create(&thread, NULL, &atenderCliente, (void*)&arguments);
		if (tp) {
			printf("ERRO: impossivel criar um thread produtor\n");
			exit(-1);
		}

		pthread_detach(thread);
	};

    /* Fecha o socket conectado ao cliente */
    close(ns);

    /* Fecha o socket aguardando por conexões */
    close(s);

    printf("Servidor terminou com sucesso.\n");
    exit(0);
}


