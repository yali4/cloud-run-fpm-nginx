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

// PHP-FPM
#define PHP_FPM "/usr/sbin/php-fpm"
#define PHP_FPM_CONF "/tmp/php-fpm.conf"
#define PHP_FPM_SOCK_PATH  "/tmp/php-fpm.sock"
#define PHP_FPM_SOCK_PATTERN "{{php-fpm.sock}}"

// NGINX
#define NGINX "/usr/sbin/nginx"
#define NGINX_CONF "/tmp/nginx.conf"

// SOCKET CONNECT INTERVAL
#define SOCK_TRY_INTERVAL 0.25

// CHILD PROCESS FINISHED INTERVAL
#define CHILD_PROCESS_INTERVAL 0.50

// CHILD PROCESS IDS
pid_t fpm_pid, nginx_pid;

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
    printf("\n Configuring PHP-FPM... \n");

    char *fpmConfig;

    fpmConfig = readFile("./php-fpm.conf");   

    fpmConfig = replaceWord(fpmConfig, PHP_FPM_SOCK_PATTERN, PHP_FPM_SOCK_PATH);

    saveFile(PHP_FPM_CONF, fpmConfig);	
}

void configureNginx()
{
    printf("\n Configuring Nginx... \n");

    char *nginxConfig;

    nginxConfig = readFile("./nginx.conf");   

    nginxConfig = replaceWord(nginxConfig, PHP_FPM_SOCK_PATTERN, PHP_FPM_SOCK_PATH);

    saveFile(NGINX_CONF, nginxConfig);	
}

void killAllChildProcesses()
{
    kill(fpm_pid, SIGTERM);
	kill(nginx_pid, SIGTERM);
    
}

void closedByUser()
{
	printf("\n Closeed by user. \n");
	killAllChildProcesses();
	exit(EXIT_SUCCESS);
}

void killedByUser()
{
	printf("\n Killed by user. \n");
	killAllChildProcesses();
	exit(EXIT_FAILURE);
}

void killedByChild()
{
	printf("\n Killed by child process. \n");
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

void tryConnectPhpFpm()
{
	int sock = 0, valread;
    struct sockaddr_un serv_addr;
    char *hello = "Hello from client";
    char buffer[1024] = {0};
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        exit(EXIT_FAILURE);
    }

    serv_addr.sun_family = AF_UNIX;
    strcpy(serv_addr.sun_path, PHP_FPM_SOCK_PATH);

    while (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\n PHP-FPM Connection Failed!\n");
        sleep(SOCK_TRY_INTERVAL);
        printf("\n Retrying... \n");
    }

    close(sock);

    printf("\n PHP-FPM is ready for incoming requests.\n");
}

int main()
{
	signal(SIGINT, closedByUser);
	signal(SIGTERM, killedByUser);

	configurePhpFpm();
	configureNginx();

    fpm_pid = fork();
    // PHP-FPM
    if (fpm_pid == 0) {

		printf("\n Starting PHP-FPM... \n");

    	execlp(PHP_FPM, PHP_FPM, "-F", "-y", PHP_FPM_CONF, "-R", NULL);

    	exit(EXIT_FAILURE);

    } else {

		nginx_pid = fork();
        // NGINX
	    if (nginx_pid == 0) {

	    	tryConnectPhpFpm();

		    printf("\n Starting Nginx... \n");

	    	execlp(NGINX, NGINX, "-c", NGINX_CONF, NULL);

	    	exit(EXIT_FAILURE);

	    }

        // MAIN
	    while (true) {

			if (isFinished(fpm_pid)) {
				printf("\n PHP-FPM trashed. \n");
				break;
			}

			if (isFinished(nginx_pid)) {
				printf("\n Nginx trashed. \n");
				break;
			}

			sleep(CHILD_PROCESS_INTERVAL);
	    }

	    killedByChild();

    }

    return 0;
}

