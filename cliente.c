/*
    CLIENTE DE TERMINAL - SISTEMA DE SENHAS

    Este programa conecta ao servidor via socket TCP,
    solicita uma senha e recebe a resposta do servidor.

    Ele é independente do front web.
    Mesmo que a interface web seja removida, este cliente continua funcionando.

    Funcionamento geral:

    1. O cliente descobre o IP do servidor.
    2. Cria um socket TCP.
    3. Conecta no servidor.
    4. Envia uma mensagem no formato:

       Solicitar senha|NOME_DA_MAQUINA

    5. O servidor gera uma senha, exemplo A001.
    6. O cliente recebe e mostra a senha no terminal.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
    Separação entre Windows e Linux.

    No Windows, sockets usam Winsock.
    No Linux, sockets usam a API POSIX/BSD.
*/
#ifdef _WIN32
    /*
        winsock2.h e ws2tcpip.h são necessários para usar sockets no Windows.
        windows.h é usado para obter o nome da máquina com GetComputerNameA().
    */
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>

    /*
        Esta linha ajuda alguns compiladores do Windows a linkar
        automaticamente a biblioteca ws2_32.
        Mesmo assim, usando GCC/MSYS2, ainda é recomendado compilar com:
        gcc cliente.c -o cliente.exe -lws2_32
    */
    #pragma comment(lib, "ws2_32.lib")

    /*
        No Windows, sockets são do tipo SOCKET.
        Criamos um apelido socket_t para deixar o código mais genérico.
    */
    typedef SOCKET socket_t;

    /*
        No Windows, sockets são fechados com closesocket().
    */
    #define CLOSESOCKET closesocket
#else
    /*
        Bibliotecas usadas para sockets no Linux.
    */
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>

    /*
        No Linux, sockets são representados por int.
    */
    typedef int socket_t;

    /*
        No Linux, sockets são fechados com close().
    */
    #define CLOSESOCKET close

    /*
        Definimos esses valores para o Linux porque no Windows
        eles já existem na Winsock.
    */
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

/*
    PORTA:
    Deve ser igual à porta usada no servidor.c.

    Se no servidor estiver:
    #define PORTA 8080

    aqui também precisa estar 8080.
*/
#define PORTA 8080

/*
    Tamanho do buffer usado para receber a resposta do servidor.
*/
#define TAM_BUFFER 1024

/*
    Inicializa os sockets.

    No Windows, é obrigatório chamar WSAStartup() antes de usar socket().
    No Linux, não é necessário fazer inicialização.
*/
void inicializar_sockets() {
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Erro ao iniciar Winsock.\n");
        exit(EXIT_FAILURE);
    }
#endif
}

/*
    Finaliza o uso dos sockets.

    No Windows, chama WSACleanup().
    No Linux, não precisa fazer nada.
*/
void finalizar_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

/*
    Obtém o nome da máquina do cliente.

    No Windows:
    - usamos GetComputerNameA()

    No Linux:
    - usamos gethostname()

    Esse nome será enviado ao servidor junto com a solicitação de senha.
    Assim o servidor consegue registrar qual máquina pediu a senha.
*/
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

    /*
        Garante que a string termine corretamente.
    */
    nome[tamanho - 1] = '\0';
}

/*
    Função principal do cliente.

    argc e argv permitem receber argumentos pelo terminal.

    Exemplo:
    ./cliente.exe 192.168.0.10

    Nesse caso:
    argv[1] = "192.168.0.10"

    Se nenhum IP for informado, o programa usa um IP padrão.
*/
int main(int argc, char *argv[]) {
    /*
        Socket do cliente.
    */
    socket_t sock;

    /*
        Estrutura que guarda o endereço do servidor:
        - família IPv4
        - porta
        - IP
    */
    struct sockaddr_in serv_addr;

    /*
        Buffer que vai guardar a resposta recebida do servidor.
    */
    char buffer[TAM_BUFFER] = {0};

    /*
        Nome da máquina local.
    */
    char nome_maquina[256];

    /*
        Mensagem que será enviada ao servidor.
        Formato esperado:

        Solicitar senha|NOME_DA_MAQUINA
    */
    char mensagem[512];

    /*
        Ponteiro para o IP do servidor.
    */
    char *ip_servidor;

    /*
        Se o usuário não passar IP pelo terminal,
        usamos um IP padrão.

        Você pode trocar esse IP conforme sua rede.

        Exemplo local:
        127.0.0.1

        Exemplo rede local:
        192.168.0.15

        Exemplo Tailscale:
        100.x.x.x
    */
    if (argc < 2) {
        ip_servidor = "192.168.137.1";
        printf("Nenhum IP informado. Usando IP padrao: %s\n", ip_servidor);
    } else {
        ip_servidor = argv[1];
    }

    /*
        Obtém o nome da máquina para enviar ao servidor.
    */
    obter_nome_maquina(nome_maquina, sizeof(nome_maquina));

    /*
        Monta a mensagem que será enviada.

        Exemplo:
        Solicitar senha|DESKTOP-ABC123
    */
    snprintf(mensagem, sizeof(mensagem), "Solicitar senha|%s", nome_maquina);

    /*
        Inicializa os sockets.
        No Windows isso chama WSAStartup().
        No Linux não faz nada.
    */
    inicializar_sockets();

    /*
        Cria o socket TCP.

        AF_INET:
        IPv4

        SOCK_STREAM:
        TCP

        0:
        protocolo padrão
    */
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == INVALID_SOCKET) {
        printf("Erro ao criar socket.\n");
        finalizar_sockets();
        return -1;
    }

    /*
        Define o tipo de endereço usado.
        AF_INET significa IPv4.
    */
    serv_addr.sin_family = AF_INET;

    /*
        Define a porta do servidor.

        htons() converte o número da porta para o formato usado pela rede.
    */
    serv_addr.sin_port = htons(PORTA);

    /*
        Converte o IP em texto para formato binário.

        Exemplo:
        "192.168.0.15" vira um endereço que o socket consegue usar.
    */
    if (inet_pton(AF_INET, ip_servidor, &serv_addr.sin_addr) <= 0) {
        printf("Endereco invalido: %s\n", ip_servidor);
        CLOSESOCKET(sock);
        finalizar_sockets();
        return -1;
    }

    /*
        Tenta conectar ao servidor.

        Se o servidor não estiver rodando,
        se o IP estiver errado,
        ou se a porta estiver bloqueada,
        connect() falha.
    */
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        printf("Conexao falhou com o servidor %s:%d\n", ip_servidor, PORTA);
        CLOSESOCKET(sock);
        finalizar_sockets();
        return -1;
    }

    /*
        Envia a solicitação de senha ao servidor.
    */
    send(sock, mensagem, (int)strlen(mensagem), 0);

    printf("Cliente: Solicitando senha como maquina %s\n", nome_maquina);

    /*
        Aguarda a resposta do servidor.

        O servidor deve responder algo como:
        A001
        A002
        A003
    */
    int bytes_recebidos = recv(sock, buffer, TAM_BUFFER - 1, 0);

    /*
        Se recebeu dados, coloca '\0' no final para transformar
        o buffer em uma string válida.
    */
    if (bytes_recebidos > 0) {
        buffer[bytes_recebidos] = '\0';
        printf("Cliente: Sua senha e %s\n", buffer);
    } else {
        printf("Erro ao receber resposta do servidor.\n");
    }

    /*
        Fecha o socket do cliente.
    */
    CLOSESOCKET(sock);

    /*
        Finaliza os sockets.
        No Windows chama WSACleanup().
    */
    finalizar_sockets();

    return 0;
}