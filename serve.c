#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/stat.h>

// PHP-FPM
#define PHP_FPM "/usr/sbin/php-fpm"
#define PHP_FPM_CONF "/tmp/php-fpm.conf"
#define PHP_FPM_SOCK_PATH  "/tmp/php-fpm.sock"
#define PHP_FPM_SOCK_PATTERN "{{php-fpm.sock}}"

// NGINX
#define NGINX "/usr/sbin/nginx"
#define NGINX_CONF "/tmp/nginx.conf"
#define NGINX_LOG_DIR "/var/log/nginx"

// SOCKET CONNECT INTERVAL (MS)
#define CONNECT_SOCK_INTERVAL 500

// CHILD PROCESS FINISHED INTERVAL (MS)
#define CHILD_PROCESS_INTERVAL 250

// CHILD PROCESS IDS
pid_t fpm_pid, nginx_pid;

void createDir(const char *directory)
{
    struct stat st = {0};

    if (stat(directory, &st) == -1) {
        mkdir(directory, 0700);
    }
}

void saveFile(const char *filepath, const char *data)
{
    FILE *fp = fopen(filepath, "w");
    if (fp != NULL)
    {
        fputs(data, fp);
        fclose(fp);
    }
}

char *readFile(const char *const filename)
{
    size_t size;
    FILE  *file;
    char  *data;

    file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("fopen()\n");
        return NULL;
    }

    /* get the file size by seeking to the end and getting the position */
    fseek(file, 0L, SEEK_END);
    size = ftell(file);
    /* reset the file position to the begining. */
    rewind(file);

    /* allocate space to hold the file content */
    data = malloc(1 + size);
    if (data == NULL)
    {
        perror("malloc()\n");
        fclose(file);
        return NULL;
    }
    /* nul terminate the content to make it a valid string */
    data[size] = '\0';
    /* attempt to read all the data */
    if (fread(data, 1, size, file) != size)
    {
        perror("fread()\n");

        free(data);
        fclose(file);

        return NULL;
    }
    fclose(file);
    return data;
}

char* replaceWord(const char* s, const char* oldW,
                  const char* newW)
{
    char* result;
    int i, cnt = 0;
    int newWlen = strlen(newW);
    int oldWlen = strlen(oldW);
  
    // Counting the number of times old word
    // occur in the string
    for (i = 0; s[i] != '\0'; i++) {
        if (strstr(&s[i], oldW) == &s[i]) {
            cnt++;
  
            // Jumping to index after the old word.
            i += oldWlen - 1;
        }
    }
  
    // Making new string of enough length
    result = (char*)malloc(i + cnt * (newWlen - oldWlen) + 1);
  
    i = 0;
    while (*s) {
        // compare the substring with the result
        if (strstr(s, oldW) == s) {
            strcpy(&result[i], newW);
            i += newWlen;
            s += oldWlen;
        }
        else
            result[i++] = *s++;
    }
  
    result[i] = '\0';
    return result;
}

void configurePhpFpm()
{
    printf("Configuring PHP-FPM... \n");

    char *fpmConfig;

    fpmConfig = readFile("./php-fpm.conf");   

    fpmConfig = replaceWord(fpmConfig, PHP_FPM_SOCK_PATTERN, PHP_FPM_SOCK_PATH);

    saveFile(PHP_FPM_CONF, fpmConfig);    
}

void configureNginx()
{
    printf("Configuring NGINX... \n");

    char *nginxConfig;

    nginxConfig = readFile("./nginx.conf");   

    nginxConfig = replaceWord(nginxConfig, PHP_FPM_SOCK_PATTERN, PHP_FPM_SOCK_PATH);

    saveFile(NGINX_CONF, nginxConfig);

    createDir(NGINX_LOG_DIR);
}

void killAllChildProcesses()
{
    kill(fpm_pid, SIGTERM);
    kill(nginx_pid, SIGTERM);
    
}

void sigintHandler()
{
    printf("Killed by SIGINT. \n");
    killAllChildProcesses();
    exit(EXIT_SUCCESS);
}

void sigtermHandler()
{
    printf("Killed by SIGTERM. \n");
    killAllChildProcesses();
    exit(EXIT_FAILURE);
}

bool isFinished(pid_t process_id)
{
    int status;
    pid_t return_pid = waitpid(process_id, &status, WNOHANG);

    if (return_pid == process_id) {
        return true;
    }

    return false;
}

void connectToPhpFpm()
{
    printf("Connecting PHP-FPM Socket... \n");

    int sock = 0;
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        printf("Error: Socket creation error! \n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un serv_addr;
    serv_addr.sun_family = AF_UNIX;
    strcpy(serv_addr.sun_path, PHP_FPM_SOCK_PATH);

    while (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Error: PHP-FPM Connection Failed! \n");
        usleep(CONNECT_SOCK_INTERVAL * 1000);
        printf("Retrying... \n");
    }

    close(sock);

    printf("PHP-FPM is ready for incoming requests. \n");
}

int main()
{
    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigtermHandler);

    configurePhpFpm();
    configureNginx();

    printf("Starting PHP-FPM... \n");

    // START PHP-FPM
    fpm_pid = fork();
    if (fpm_pid == 0) {

        execlp(PHP_FPM, PHP_FPM, "-F", "-y", PHP_FPM_CONF, "-R", NULL);
        exit(EXIT_FAILURE);

    } else {

        // CONNECT PHP-FPM BEFORE START NGINX
        connectToPhpFpm();

        printf("Starting NGINX... \n");

        // START NGINX
        nginx_pid = fork();
        if (nginx_pid == 0) {

            execlp(NGINX, NGINX, "-c", NGINX_CONF, NULL);
            exit(EXIT_FAILURE);

        }

        // MAIN APP
        while (true) {

            if (isFinished(fpm_pid)) {
                printf("Error: PHP-FPM crashed! \n");
                break;
            }

            if (isFinished(nginx_pid)) {
                printf("Error: NGINX crashed! \n");
                break;
            }

            usleep(CHILD_PROCESS_INTERVAL * 1000);
        }

        kill(getpid(), SIGTERM);

    }

    return 0;
}

