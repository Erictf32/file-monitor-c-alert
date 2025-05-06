#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#define MAX_ARQUIVOS 1000
#define TAMANHO_MAX_CAMINHO 1024
#define TAMANHO_BUFFER 1024

/* ========= CONFIGURAÇÕES DE E-MAIL ========= */
/* MODIFIQUE ESTES VALORES COM SUAS INFORMAÇÕES */
#define SMTP_SERVER "smtp.gmail.com"
#define SMTP_PORT 587
#define EMAIL_FROM "bililopes2@gmail.com" // E-mail que enviará as notificacoes
#define EMAIL_PASSWORD "dlqv tyfz zdlx wdma"  // Use senha de app para Gmail
#define EMAIL_TO "papadefala@gmail.com" // E-mail que receberá as notificacoes
#define ASSUNTO_PADRAO "[ALERTA] Alteracao detectada no sistema" // Título do e-mail

typedef struct {
    char caminho[TAMANHO_MAX_CAMINHO];
    long tamanho_arquivo;
    time_t ultima_modificacao;
} InformacaoArquivo;

InformacaoArquivo banco_dados_arquivos[MAX_ARQUIVOS];
int total_arquivos = 0;
int executando = 1;

// ========== FUNÇÕES AUXILIARES ==========

const char* get_current_time() {
    static char buffer[20];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, 20, "%H:%M:%S", tm_info);
    return buffer;
}

#ifndef _WIN32
void configurar_terminal() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

int getch() {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

// ========== FUNÇÕES DE E-MAIL ==========

void enviar_alerta_email(const char* assunto, const char* mensagem) {
    #ifdef _WIN32
    // 1. Criar arquivos temporários
    FILE *temp_msg = fopen("email_msg.txt", "w");
    if (!temp_msg) {
        perror("Erro ao criar arquivo de mensagem");
        return;
    }
    fprintf(temp_msg, "%s", mensagem);
    fclose(temp_msg);
    
    FILE *temp_ps1 = fopen("send_email.ps1", "w");
    if (!temp_ps1) {
        perror("Erro ao criar script PowerShell");
        remove("email_msg.txt");
        return;
    }
    
    // 2. Gerar script PowerShell robusto
    fprintf(temp_ps1, 
        "try {\n"
        "    $EmailFrom = '%s'\n"
        "    $EmailTo = '%s'\n"
        "    $Subject = '%s'\n"
        "    $Body = Get-Content 'email_msg.txt' -Raw\n"
        "    $SMTPServer = '%s'\n"
        "    $SMTPPort = %d\n"
        "    $Username = '%s'\n"
        "    $Password = '%s'\n"
        "\n"
        "    # Configuração segura\n"
        "    $SMTPClient = New-Object Net.Mail.SmtpClient($SMTPServer, $SMTPPort)\n"
        "    $SMTPClient.EnableSsl = $true\n"
        "    $SMTPClient.Credentials = New-Object System.Net.NetworkCredential($Username, $Password)\n"
        "\n"
        "    # Criar mensagem\n"
        "    $MailMessage = New-Object Net.Mail.MailMessage($EmailFrom, $EmailTo, $Subject, $Body)\n"
        "\n"
        "    # Enviar com timeout\n"
        "    $SMTPClient.Timeout = 10000 # 10 segundos\n"
        "    $SMTPClient.Send($MailMessage)\n"
        "    Write-Host 'E-mail enviado com sucesso para ' + $EmailTo\n"
        "} catch {\n"
        "    Write-Host 'ERRO: ' + $_.Exception.Message\n"
        "    exit 1\n"
        "} finally {\n"
        "    Remove-Item 'email_msg.txt' -ErrorAction SilentlyContinue\n"
        "}", 
        EMAIL_FROM, EMAIL_TO, assunto, SMTP_SERVER, SMTP_PORT, EMAIL_FROM, EMAIL_PASSWORD);
    
    fclose(temp_ps1);
    
    // 3. Executar script
    system("powershell -ExecutionPolicy Bypass -File send_email.ps1");
    
    // 4. Limpeza
    remove("send_email.ps1");
    #else
    char comando[TAMANHO_BUFFER * 2];
    snprintf(comando, sizeof(comando),
        "echo -e \"%s\" | mail -s \"%s\" %s",
        mensagem, assunto, EMAIL_TO);
    system(comando);
    #endif
    
    printf("\nE-mail de alerta enviado para %s\n", EMAIL_TO);
}

// ========== FUNÇÕES DE MONITORAMENTO ==========

void registrar_mudanca(const char *nome_arquivo, const char *tipo_mudanca, const char *detalhes) {
    time_t agora = time(NULL);
    char *texto_tempo = ctime(&agora);
    texto_tempo[strlen(texto_tempo)-1] = '\0';

    // Prepara mensagem para e-mail
    char email_mensagem[TAMANHO_BUFFER * 2];
    snprintf(email_mensagem, sizeof(email_mensagem),
        "Alerta de Seguranca\n\n"
        "Tipo: %s\n"
        "Arquivo: %s\n"
        "Horario: %s\n"
        "Detalhes: %s\n\n"
        "--\n"
        "Sistema de Monitoramento Automatico",
        tipo_mudanca, nome_arquivo, texto_tempo, detalhes ? detalhes : "Nenhum detalhe adicional");

    // Envia e-mail
    enviar_alerta_email(ASSUNTO_PADRAO, email_mensagem);

    // Registra no log local
    FILE *arquivo_log = fopen("mudancas_arquivos.log", "a");
    if (arquivo_log) {
        fprintf(arquivo_log, "[%s] Alteracao detectada: %s\n", texto_tempo, nome_arquivo);
        fprintf(arquivo_log, "Tipo: %s\n", tipo_mudanca);
        if (detalhes) fprintf(arquivo_log, "Detalhes: %s\n", detalhes);
        fprintf(arquivo_log, "----------------------------------------\n");
        fclose(arquivo_log);
    }
}

void verificar_diretorio(const char *caminho) {
    DIR *diretorio = opendir(caminho);
    if (!diretorio) {
        perror("Erro ao abrir diretorio");
        return;
    }

    struct dirent *entrada;
    while ((entrada = readdir(diretorio)) != NULL) {
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0) {
            continue;
        }

        char caminho_completo[TAMANHO_MAX_CAMINHO];
        snprintf(caminho_completo, sizeof(caminho_completo), "%s/%s", caminho, entrada->d_name);

        struct stat info_arquivo;
        if (stat(caminho_completo, &info_arquivo)) {
            perror("Erro ao obter informacoes do arquivo");
            continue;
        }

        if (S_ISDIR(info_arquivo.st_mode)) {
            verificar_diretorio(caminho_completo);
            continue;
        }

        int encontrado = 0;
        for (int i = 0; i < total_arquivos; i++) {
            if (strcmp(banco_dados_arquivos[i].caminho, caminho_completo) == 0) {
                encontrado = 1;
                
                if (banco_dados_arquivos[i].ultima_modificacao != info_arquivo.st_mtime || 
                    banco_dados_arquivos[i].tamanho_arquivo != info_arquivo.st_size) {
                    
                    char detalhes[100];
                    snprintf(detalhes, sizeof(detalhes), 
                            "Modificado em: %ld, Tamanho: %ld -> %ld", 
                            info_arquivo.st_mtime, 
                            banco_dados_arquivos[i].tamanho_arquivo, 
                            info_arquivo.st_size);
                    
                    printf("\nArquivo modificado: %s\n", caminho_completo);
                    registrar_mudanca(caminho_completo, "MODIFICADO", detalhes);
                    
                    banco_dados_arquivos[i].ultima_modificacao = info_arquivo.st_mtime;
                    banco_dados_arquivos[i].tamanho_arquivo = info_arquivo.st_size;
                }
                break;
            }
        }

        if (!encontrado && total_arquivos < MAX_ARQUIVOS) {
            strcpy(banco_dados_arquivos[total_arquivos].caminho, caminho_completo);
            banco_dados_arquivos[total_arquivos].ultima_modificacao = info_arquivo.st_mtime;
            banco_dados_arquivos[total_arquivos].tamanho_arquivo = info_arquivo.st_size;
            
            printf("\nNovo arquivo detectado: %s\n", caminho_completo);
            registrar_mudanca(caminho_completo, "CRIADO", NULL);
            
            total_arquivos++;
        }
    }

    closedir(diretorio);
}

void salvar_banco_dados() {
    FILE *arquivo = fopen("banco_dados.dat", "wb");
    if (!arquivo) {
        perror("Erro ao salvar banco de dados");
        return;
    }
    
    fwrite(&total_arquivos, sizeof(int), 1, arquivo);
    fwrite(banco_dados_arquivos, sizeof(InformacaoArquivo), total_arquivos, arquivo);
    
    fclose(arquivo);
}

void carregar_banco_dados() {
    FILE *arquivo = fopen("banco_dados.dat", "rb");
    if (!arquivo) {
        printf("Banco de dados NAO encontrado. Iniciando novo monitoramento.\n");
        return;
    }
    
    fread(&total_arquivos, sizeof(int), 1, arquivo);
    fread(banco_dados_arquivos, sizeof(InformacaoArquivo), total_arquivos, arquivo);
    
    fclose(arquivo);
}

void verificar_arquivos_removidos() {
    for (int i = 0; i < total_arquivos; i++) {
        if (banco_dados_arquivos[i].caminho[0] != '\0') {
            struct stat info_arquivo;
            if (stat(banco_dados_arquivos[i].caminho, &info_arquivo) == -1) {
                printf("\nArquivo removido: %s\n", banco_dados_arquivos[i].caminho);
                registrar_mudanca(banco_dados_arquivos[i].caminho, "REMOVIDO", NULL);
                
                banco_dados_arquivos[i].caminho[0] = '\0';
            }
        }
    }
}

void mostrar_arquivos_ativos() {
    printf("\n=== ARQUIVOS MONITORADOS ===\n");
    for (int i = 0; i < total_arquivos; i++) {
        if (banco_dados_arquivos[i].caminho[0] != '\0') {
            struct tm *tm_info = localtime(&banco_dados_arquivos[i].ultima_modificacao);
            char buffer[30];
            strftime(buffer, 30, "%Y-%m-%d %H:%M:%S", tm_info);
            
            printf("- %s\n", banco_dados_arquivos[i].caminho);
            printf("  Tamanho: %ld bytes | Modificado: %s\n\n", 
                  banco_dados_arquivos[i].tamanho_arquivo, buffer);
        }
    }
    printf("Total: %d arquivos\n", total_arquivos);
}

void verificar_teclado() {
    #ifdef _WIN32
    if (GetAsyncKeyState('M') & 0x8000 && GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        mostrar_arquivos_ativos();
    }
    #else
    if (getch() == '\x0d') {  // Ctrl+M no Linux
        mostrar_arquivos_ativos();
    }
    #endif
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <diretorio>\n", argv[0]);
        return 1;
    }

    #ifndef _WIN32
    configurar_terminal();
    #endif

    carregar_banco_dados();

    printf("\n=== MONITOR DE ARQUIVOS ===\n");
    printf("Diretorio monitorado: %s\n", argv[1]);
    printf("Alertas sendo enviados para: %s\n", EMAIL_TO);
    printf("Comandos:\n");
    printf("  Ctrl+M - Listar arquivos monitorados\n");
    printf("  Ctrl+C - Sair do programa\n\n");

    while (1) {
        printf("\r[%s] Monitorando... (Ctrl+M para menu)", get_current_time());
        fflush(stdout);

        verificar_diretorio(argv[1]);
        verificar_arquivos_removidos();
        verificar_teclado();
        salvar_banco_dados();

        #ifdef _WIN32
            Sleep(5000);
        #else
            sleep(5);
        #endif
    }

    return 0;
}