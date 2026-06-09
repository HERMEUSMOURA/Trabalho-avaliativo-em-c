#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")

    typedef SOCKET socket_t;
    #define CLOSESOCKET closesocket
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>

    typedef int socket_t;
    #define CLOSESOCKET close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#define PORTA 8080
#define TAM_BUFFER 1024

void inicializar_sockets() {
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Erro ao iniciar Winsock.\n");
        exit(EXIT_FAILURE);
    }
#endif
}

void finalizar_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void obter_nome_maquina(char *nome, int tamanho) {
#ifdef _WIN32
    DWORD tamanho_nome = tamanho;

    if (!GetComputerNameA(nome, &tamanho_nome)) {
        snprintf(nome, tamanho, "Cliente desconhecido");
    }
#else
    if (gethostname(nome, tamanho) != 0) {
        snprintf(nome, tamanho, "Cliente desconhecido");
    }
#endif

    nome[tamanho - 1] = '\0';
}

int main(int argc, char *argv[]) {
    socket_t sock;
    struct sockaddr_in serv_addr;
    char buffer[TAM_BUFFER] = {0};

    char nome_maquina[256];
    char mensagem[512];

    char *ip_servidor;

    if (argc < 2) {
        ip_servidor = "192.168.137.1";
        printf("Nenhum IP informado. Usando IP padrao: %s\n", ip_servidor);
    } else {
        ip_servidor = argv[1];
    }

    obter_nome_maquina(nome_maquina, sizeof(nome_maquina));

    snprintf(mensagem, sizeof(mensagem), "Solicitar senha|%s", nome_maquina);

    inicializar_sockets();

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == INVALID_SOCKET) {
        printf("Erro ao criar socket.\n");
        finalizar_sockets();
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORTA);

    if (inet_pton(AF_INET, ip_servidor, &serv_addr.sin_addr) <= 0) {
        printf("Endereco invalido: %s\n", ip_servidor);
        CLOSESOCKET(sock);
        finalizar_sockets();
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        printf("Conexao falhou com o servidor %s:%d\n", ip_servidor, PORTA);
        CLOSESOCKET(sock);
        finalizar_sockets();
        return -1;
    }

    send(sock, mensagem, (int)strlen(mensagem), 0);

    printf("Cliente: Solicitando senha como maquina %s\n", nome_maquina);

    int bytes_recebidos = recv(sock, buffer, TAM_BUFFER - 1, 0);

    if (bytes_recebidos > 0) {
        buffer[bytes_recebidos] = '\0';
        printf("Cliente: Sua senha e %s\n", buffer);
    } else {
        printf("Erro ao receber resposta do servidor.\n");
    }

    CLOSESOCKET(sock);
    finalizar_sockets();

    return 0;
}