#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
    #include <pthread.h>

    typedef int socket_t;
    #define CLOSESOCKET close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#define PORTA 8080
#define MAX_CLIENTES 10
#define TAM_BUFFER 8192

int contador = 1;
char ultima_senha[20] = "Nenhuma";
char ultima_maquina[256] = "Nenhuma";

#ifdef _WIN32
    CRITICAL_SECTION mutex_contador;
#else
    pthread_mutex_t mutex_contador = PTHREAD_MUTEX_INITIALIZER;
#endif

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

void bloquear_mutex() {
#ifdef _WIN32
    EnterCriticalSection(&mutex_contador);
#else
    pthread_mutex_lock(&mutex_contador);
#endif
}

void liberar_mutex() {
#ifdef _WIN32
    LeaveCriticalSection(&mutex_contador);
#else
    pthread_mutex_unlock(&mutex_contador);
#endif
}

void enviar_texto(socket_t cliente_socket, const char *texto) {
    send(cliente_socket, texto, (int)strlen(texto), 0);
}

void gerar_senha(const char *nome_maquina, char *senha_saida, int tamanho_senha) {
    bloquear_mutex();

    snprintf(senha_saida, tamanho_senha, "A%03d", contador);
    contador++;

    snprintf(ultima_senha, sizeof(ultima_senha), "%s", senha_saida);
    snprintf(ultima_maquina, sizeof(ultima_maquina), "%s", nome_maquina);

    liberar_mutex();

    printf("Servidor: Senha gerada: %s para %s\n", senha_saida, nome_maquina);
}

void url_decode(char *destino, const char *origem, int tamanho_destino) {
    int i = 0;
    int j = 0;

    while (origem[i] != '\0' && j < tamanho_destino - 1) {
        if (
            origem[i] == '%' &&
            isxdigit((unsigned char)origem[i + 1]) &&
            isxdigit((unsigned char)origem[i + 2])
        ) {
            char hex[3];

            hex[0] = origem[i + 1];
            hex[1] = origem[i + 2];
            hex[2] = '\0';

            destino[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (origem[i] == '+') {
            destino[j++] = ' ';
            i++;
        } else {
            destino[j++] = origem[i];
            i++;
        }
    }

    destino[j] = '\0';
}

void obter_metodo_http(const char *requisicao, char *metodo, int tamanho_metodo) {
    const char *fim = strchr(requisicao, ' ');

    if (fim == NULL) {
        snprintf(metodo, tamanho_metodo, "");
        return;
    }

    int tamanho = (int)(fim - requisicao);

    if (tamanho >= tamanho_metodo) {
        tamanho = tamanho_metodo - 1;
    }

    strncpy(metodo, requisicao, tamanho);
    metodo[tamanho] = '\0';
}

void obter_caminho_http(const char *requisicao, char *caminho, int tamanho_caminho) {
    const char *inicio = strchr(requisicao, ' ');

    if (inicio == NULL) {
        snprintf(caminho, tamanho_caminho, "/");
        return;
    }

    inicio++;

    const char *fim = strchr(inicio, ' ');

    if (fim == NULL) {
        snprintf(caminho, tamanho_caminho, "/");
        return;
    }

    int tamanho = (int)(fim - inicio);

    if (tamanho >= tamanho_caminho) {
        tamanho = tamanho_caminho - 1;
    }

    strncpy(caminho, inicio, tamanho);
    caminho[tamanho] = '\0';
}

void obter_corpo_http(const char *requisicao, char *corpo, int tamanho_corpo) {
    const char *inicio = strstr(requisicao, "\r\n\r\n");

    if (inicio == NULL) {
        snprintf(corpo, tamanho_corpo, "");
        return;
    }

    inicio += 4;

    snprintf(corpo, tamanho_corpo, "%s", inicio);
}

void obter_parametro_maquina(const char *texto, char *maquina, int tamanho_maquina) {
    char *inicio = strstr(texto, "maquina=");

    if (inicio == NULL) {
        snprintf(maquina, tamanho_maquina, "Navegador");
        return;
    }

    inicio += strlen("maquina=");

    char valor_codificado[256];
    int i = 0;

    while (
        inicio[i] != '\0' &&
        inicio[i] != ' ' &&
        inicio[i] != '&' &&
        inicio[i] != '\r' &&
        inicio[i] != '\n' &&
        i < (int)sizeof(valor_codificado) - 1
    ) {
        valor_codificado[i] = inicio[i];
        i++;
    }

    valor_codificado[i] = '\0';

    url_decode(maquina, valor_codificado, tamanho_maquina);

    if (strlen(maquina) == 0) {
        snprintf(maquina, tamanho_maquina, "Navegador");
    }
}

void enviar_resposta_http(socket_t cliente_socket, const char *status, const char *tipo, const char *conteudo) {
    char cabecalho[512];

    snprintf(
        cabecalho,
        sizeof(cabecalho),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        tipo,
        strlen(conteudo)
    );

    enviar_texto(cliente_socket, cabecalho);
    enviar_texto(cliente_socket, conteudo);
}

void enviar_200(socket_t cliente_socket, const char *tipo, const char *conteudo) {
    enviar_resposta_http(cliente_socket, "200 OK", tipo, conteudo);
}

void enviar_404(socket_t cliente_socket) {
    const char *conteudo =
        "<!DOCTYPE html>"
        "<html lang='pt-br'>"
        "<head>"
        "<meta charset='utf-8'>"
        "<title>404</title>"
        "</head>"
        "<body>"
        "<h1>404 - Pagina nao encontrada</h1>"
        "</body>"
        "</html>";

    enviar_resposta_http(cliente_socket, "404 Not Found", "text/html", conteudo);
}

void enviar_405(socket_t cliente_socket) {
    const char *conteudo = "Metodo nao permitido.";

    enviar_resposta_http(cliente_socket, "405 Method Not Allowed", "text/plain", conteudo);
}

void pagina_inicial(socket_t cliente_socket) {
    const char *html =
        "<!DOCTYPE html>"
        "<html lang='pt-br'>"
        "<head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Sistema de Senhas</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#111827;color:white;margin:0;display:flex;align-items:center;justify-content:center;min-height:100vh;}"
        ".card{background:#1f2937;padding:32px;border-radius:16px;text-align:center;box-shadow:0 0 20px #0008;max-width:420px;width:90%;}"
        "h1{margin-top:0;font-size:32px;}"
        "a{display:block;background:#2563eb;color:white;text-decoration:none;padding:16px;margin:16px 0;border-radius:10px;font-size:22px;}"
        "a:hover{background:#1d4ed8;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "<h1>Sistema de Senhas</h1>"
        "<a href='/cliente'>Abrir Cliente</a>"
        "<a href='/painel'>Abrir Painel</a>"
        "</div>"
        "</body>"
        "</html>";

    enviar_200(cliente_socket, "text/html", html);
}

void pagina_cliente(socket_t cliente_socket) {
    const char *html =
        "<!DOCTYPE html>"
        "<html lang='pt-br'>"
        "<head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Cliente - Solicitar Senha</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#f3f4f6;margin:0;display:flex;align-items:center;justify-content:center;min-height:100vh;}"
        ".card{background:white;padding:32px;border-radius:16px;box-shadow:0 0 20px #0002;text-align:center;max-width:420px;width:90%;}"
        "h1{font-size:30px;margin-top:0;color:#111827;}"
        "label{display:block;text-align:left;margin-top:16px;color:#374151;font-weight:bold;}"
        "input{width:100%;padding:14px;font-size:18px;border:1px solid #d1d5db;border-radius:8px;box-sizing:border-box;margin-top:6px;}"
        "button{width:100%;padding:16px;margin-top:24px;font-size:22px;background:#2563eb;color:white;border:none;border-radius:10px;cursor:pointer;}"
        "button:hover{background:#1d4ed8;}"
        "#resultado{font-size:32px;font-weight:bold;margin-top:24px;color:#111827;}"
        "#status{font-size:14px;color:#6b7280;margin-top:12px;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "<h1>Solicitar Senha</h1>"
        "<label>Nome da maquina / setor:</label>"
        "<input id='maquina' placeholder='Ex: Recepcao 1'>"
        "<button id='btn' onclick='solicitarSenha()'>Solicitar senha</button>"
        "<div id='resultado'>---</div>"
        "<div id='status'></div>"
        "</div>"
        "<script>"
        "document.getElementById('maquina').value = localStorage.getItem('nomeMaquina') || '';"
        "async function solicitarSenha(){"
        "let botao=document.getElementById('btn');"
        "botao.disabled=true;"
        "botao.innerText='Solicitando...';"
        "let maquina=document.getElementById('maquina').value.trim();"
        "if(!maquina){maquina='Navegador';}"
        "localStorage.setItem('nomeMaquina', maquina);"
        "try{"
        "let corpo='maquina='+encodeURIComponent(maquina);"
        "let resp=await fetch('/api/solicitar',{"
        "method:'POST',"
        "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:corpo"
        "});"
        "let texto=await resp.text();"
        "document.getElementById('resultado').innerText=texto;"
        "document.getElementById('status').innerText='Senha solicitada com sucesso.';"
        "}catch(e){"
        "document.getElementById('status').innerText='Erro ao conectar ao servidor.';"
        "}"
        "botao.disabled=false;"
        "botao.innerText='Solicitar senha';"
        "}"
        "</script>"
        "</body>"
        "</html>";

    enviar_200(cliente_socket, "text/html", html);
}

void pagina_painel(socket_t cliente_socket) {
    const char *html =
        "<!DOCTYPE html>"
        "<html lang='pt-br'>"
        "<head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Painel de Chamada</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#020617;color:white;margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;}"
        ".painel{text-align:center;width:100%;padding:40px;box-sizing:border-box;}"
        ".titulo{font-size:52px;font-weight:bold;margin-bottom:40px;color:#93c5fd;}"
        ".senha-label{font-size:48px;color:#cbd5e1;}"
        "#senha{font-size:130px;font-weight:bold;color:#facc15;margin:20px 0;}"
        ".maquina-label{font-size:36px;color:#cbd5e1;margin-top:30px;}"
        "#maquina{font-size:54px;font-weight:bold;color:#ffffff;margin-top:10px;}"
        ".status{font-size:20px;color:#94a3b8;margin-top:40px;}"
        "button{font-size:20px;padding:12px 20px;border:0;border-radius:8px;background:#2563eb;color:white;cursor:pointer;margin-top:20px;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='painel'>"
        "<div class='titulo'>PAINEL DE CHAMADA</div>"
        "<div class='senha-label'>SENHA</div>"
        "<div id='senha'>---</div>"
        "<div class='maquina-label'>MAQUINA / SETOR</div>"
        "<div id='maquina'>---</div>"
        "<button onclick='ativarVoz()'>Ativar voz</button>"
        "<div class='status' id='status'>Aguardando chamadas...</div>"
        "</div>"
        "<script>"
        "let ultimaSenha='';"
        "let ultimaMaquina='';"
        "let vozAtiva=false;"
        "function ativarVoz(){"
        "vozAtiva=true;"
        "let u=new SpeechSynthesisUtterance('Voz ativada');"
        "u.lang='pt-BR';"
        "speechSynthesis.speak(u);"
        "document.getElementById('status').innerText='Voz ativada. Aguardando chamadas...';"
        "}"
        "function falar(senha,maquina){"
        "if(!vozAtiva)return;"
        "let texto='Senha '+senha.split('').join(' ')+', solicitada por '+maquina;"
        "let u=new SpeechSynthesisUtterance(texto);"
        "u.lang='pt-BR';"
        "u.rate=0.85;"
        "speechSynthesis.speak(u);"
        "}"
        "async function atualizar(){"
        "try{"
        "let resp=await fetch('/api/ultima');"
        "let texto=await resp.text();"
        "let partes=texto.split('|');"
        "let senha=partes[0]||'Nenhuma';"
        "let maquina=partes[1]||'Nenhuma';"
        "document.getElementById('status').innerText='Conectado ao servidor';"
        "if(senha!==ultimaSenha || maquina!==ultimaMaquina){"
        "ultimaSenha=senha;"
        "ultimaMaquina=maquina;"
        "document.getElementById('senha').innerText=senha;"
        "document.getElementById('maquina').innerText=maquina;"
        "if(senha!=='Nenhuma' && senha!=='Erro' && senha!=='Sem conexao'){"
        "falar(senha,maquina);"
        "}"
        "}"
        "}catch(e){"
        "document.getElementById('status').innerText='Sem conexao com o servidor';"
        "}"
        "}"
        "setInterval(atualizar,2000);"
        "atualizar();"
        "</script>"
        "</body>"
        "</html>";

    enviar_200(cliente_socket, "text/html", html);
}

void tratar_requisicao_http(socket_t cliente_socket, char *buffer) {
    char metodo[16];
    char caminho[512];

    obter_metodo_http(buffer, metodo, sizeof(metodo));
    obter_caminho_http(buffer, caminho, sizeof(caminho));

    printf("HTTP: %s %s\n", metodo, caminho);

    if (strcmp(metodo, "GET") == 0 && strcmp(caminho, "/") == 0) {
        pagina_inicial(cliente_socket);
        return;
    }

    if (strcmp(metodo, "GET") == 0 && strcmp(caminho, "/cliente") == 0) {
        pagina_cliente(cliente_socket);
        return;
    }

    if (strcmp(metodo, "GET") == 0 && strcmp(caminho, "/painel") == 0) {
        pagina_painel(cliente_socket);
        return;
    }

    if (strcmp(metodo, "GET") == 0 && strcmp(caminho, "/api/ultima") == 0) {
        char resposta[300];

        bloquear_mutex();

        snprintf(resposta, sizeof(resposta), "%s|%s", ultima_senha, ultima_maquina);

        liberar_mutex();

        enviar_200(cliente_socket, "text/plain", resposta);
        return;
    }

    /*
        Proteção importante:
        /api/solicitar só aceita POST.

        Abrir no navegador por GET não gera senha.
    */
    if (strcmp(caminho, "/api/solicitar") == 0) {
        if (strcmp(metodo, "POST") != 0) {
            enviar_405(cliente_socket);
            return;
        }

        char corpo[1024];
        char maquina[256];
        char senha[20];

        obter_corpo_http(buffer, corpo, sizeof(corpo));
        obter_parametro_maquina(corpo, maquina, sizeof(maquina));

        printf("Web: Maquina %s solicitou senha.\n", maquina);

        gerar_senha(maquina, senha, sizeof(senha));

        enviar_200(cliente_socket, "text/plain", senha);
        return;
    }

    if (strcmp(metodo, "GET") == 0 && strcmp(caminho, "/favicon.ico") == 0) {
        enviar_200(cliente_socket, "text/plain", "");
        return;
    }

    enviar_404(cliente_socket);
}

void imprimir_mensagem_ignorada(const char *buffer) {
    printf("Mensagem desconhecida ignorada: ");

    for (int i = 0; buffer[i] != '\0' && i < 80; i++) {
        unsigned char c = (unsigned char)buffer[i];

        if (c >= 32 && c <= 126) {
            printf("%c", c);
        } else {
            printf(".");
        }
    }

    printf("\n");
}

void tratar_requisicao_terminal(socket_t cliente_socket, char *buffer) {
    if (strcmp(buffer, "ULTIMA") == 0) {
        char resposta[300];

        bloquear_mutex();

        snprintf(resposta, sizeof(resposta), "%s|%s", ultima_senha, ultima_maquina);

        liberar_mutex();

        send(cliente_socket, resposta, (int)strlen(resposta), 0);

        CLOSESOCKET(cliente_socket);
        return;
    }

    /*
        Proteção:
        só gera senha via terminal se a mensagem começar exatamente com:
        "Solicitar senha"
    */
    if (strncmp(buffer, "Solicitar senha", 15) != 0) {
        imprimir_mensagem_ignorada(buffer);
        CLOSESOCKET(cliente_socket);
        return;
    }

    char nome_recebido[256] = "Cliente desconhecido";
    char *separador = strchr(buffer, '|');

    if (separador != NULL) {
        separador++;

        if (strlen(separador) > 0) {
            snprintf(nome_recebido, sizeof(nome_recebido), "%s", separador);
        }

        printf("Terminal: Maquina %s solicitou senha.\n", nome_recebido);
    } else {
        printf("Terminal: Cliente solicitou senha.\n");
    }

    char senha[20];

    gerar_senha(nome_recebido, senha, sizeof(senha));

    send(cliente_socket, senha, (int)strlen(senha), 0);

    CLOSESOCKET(cliente_socket);
}

void tratar_cliente(socket_t cliente_socket) {
    char buffer[TAM_BUFFER] = {0};

    int bytes_recebidos = recv(cliente_socket, buffer, TAM_BUFFER - 1, 0);

    if (bytes_recebidos <= 0) {
        printf("Erro ao receber dados.\n");
        CLOSESOCKET(cliente_socket);
        return;
    }

    buffer[bytes_recebidos] = '\0';

    if (
        strncmp(buffer, "GET ", 4) == 0 ||
        strncmp(buffer, "POST ", 5) == 0
    ) {
        tratar_requisicao_http(cliente_socket, buffer);
        CLOSESOCKET(cliente_socket);
        return;
    }

    tratar_requisicao_terminal(cliente_socket, buffer);
}

#ifdef _WIN32
DWORD WINAPI thread_cliente(LPVOID arg) {
    socket_t cliente_socket = *(socket_t *)arg;
    free(arg);

    tratar_cliente(cliente_socket);

    return 0;
}
#else
void *thread_cliente(void *arg) {
    socket_t cliente_socket = *(socket_t *)arg;
    free(arg);

    tratar_cliente(cliente_socket);

    return NULL;
}
#endif

int main() {
    socket_t servidor_fd;
    struct sockaddr_in endereco;
    int opt = 1;

    inicializar_sockets();

#ifdef _WIN32
    InitializeCriticalSection(&mutex_contador);
#endif

    servidor_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (servidor_fd == INVALID_SOCKET) {
        printf("Erro ao criar socket.\n");
        finalizar_sockets();
        exit(EXIT_FAILURE);
    }

#ifdef _WIN32
    setsockopt(
        servidor_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&opt,
        sizeof(opt)
    );
#else
    setsockopt(
        servidor_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );
#endif

    endereco.sin_family = AF_INET;
    endereco.sin_addr.s_addr = INADDR_ANY;
    endereco.sin_port = htons(PORTA);

    if (bind(servidor_fd, (struct sockaddr *)&endereco, sizeof(endereco)) == SOCKET_ERROR) {
        printf("Erro no bind.\n");
        CLOSESOCKET(servidor_fd);
        finalizar_sockets();
        exit(EXIT_FAILURE);
    }

    if (listen(servidor_fd, MAX_CLIENTES) == SOCKET_ERROR) {
        printf("Erro no listen.\n");
        CLOSESOCKET(servidor_fd);
        finalizar_sockets();
        exit(EXIT_FAILURE);
    }

    printf("Servidor iniciado na porta %d...\n", PORTA);
    printf("Terminal continua funcionando normalmente.\n");
    printf("Web disponivel em:\n");
    printf("  http://localhost:%d/\n", PORTA);
    printf("  http://localhost:%d/cliente\n", PORTA);
    printf("  http://localhost:%d/painel\n", PORTA);
    printf("Aguardando conexoes...\n");

    while (1) {
        struct sockaddr_in endereco_cliente;
        socklen_t addrlen = sizeof(endereco_cliente);

        socket_t novo_socket = accept(
            servidor_fd,
            (struct sockaddr *)&endereco_cliente,
            &addrlen
        );

        if (novo_socket == INVALID_SOCKET) {
            printf("Erro no accept.\n");
            continue;
        }

        socket_t *cliente_ptr = malloc(sizeof(socket_t));

        if (cliente_ptr == NULL) {
            printf("Erro de memoria.\n");
            CLOSESOCKET(novo_socket);
            continue;
        }

        *cliente_ptr = novo_socket;

#ifdef _WIN32
        HANDLE thread = CreateThread(
            NULL,
            0,
            thread_cliente,
            cliente_ptr,
            0,
            NULL
        );

        if (thread == NULL) {
            printf("Erro ao criar thread.\n");
            CLOSESOCKET(novo_socket);
            free(cliente_ptr);
        } else {
            CloseHandle(thread);
        }
#else
        pthread_t thread;

        if (pthread_create(&thread, NULL, thread_cliente, cliente_ptr) != 0) {
            printf("Erro ao criar thread.\n");
            CLOSESOCKET(novo_socket);
            free(cliente_ptr);
        } else {
            pthread_detach(thread);
        }
#endif
    }

    CLOSESOCKET(servidor_fd);

#ifdef _WIN32
    DeleteCriticalSection(&mutex_contador);
#endif

    finalizar_sockets();

    return 0;
}