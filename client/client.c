#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOGIN_SUCCESSED_MESSAGE "[Client] Login successfully."
#define LOGIN_FAILED_MESSAGE "[Client] Login failed."
#define DISCONNECT_MESSAGE "[Client] You have been disconnected."
#define PERMISSION_DENIED_MESSAGE "[Client] You don't have permission."
#define FILE_DOES_NOT_EXIST_MESSAGE "[Client] File doesn't exist."
#define REMOTE_FILE_DOES_NOT_EXIST_MESSAGE "[Client] Remote file doesn't exist."
#define REMOTE_FILE_IS_BUSY_MESSAGE "[Client] Remote file is busy, try again later."
#define TRANSMISSION_ERROR_MESSAGE "[Client] Transmission error."

void sendMessage(int socketStream, char *sendingMessage)
{
    int sentLength = 0;
    int totalLength = 0;
    int messageLength = strlen(sendingMessage) + 1;

    while (totalLength != messageLength)
    {
        sentLength = send(socketStream, &sendingMessage[totalLength], strlen(sendingMessage) + 1 - totalLength, 0);

        if (sentLength == -1)
        {
            printf("%s\n", TRANSMISSION_ERROR_MESSAGE);
            break;
        }

        totalLength += sentLength;
    }

    sleep(1);
}

void receiveMessage(int socketStream, char *receivedMessage)
{
    int notFound = 1;
    int receivedLength = 0;
    int totalLength = 0;
    char buffer[256] = {0};

    bzero(receivedMessage, 256);

    while (notFound)
    {
        receivedLength = recv(socketStream, &buffer[totalLength], sizeof(buffer) - totalLength - 1, 0);

        if (receivedLength < 0)
        {
            printf("%s\n", TRANSMISSION_ERROR_MESSAGE);
            break;
        }

        totalLength += receivedLength;
        buffer[totalLength] = '\0';
        if (strchr(buffer, '\n') != 0)
        {
            notFound = 0;
        }
    }

    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(receivedMessage, buffer);
}

int main()
{
    int socketStream = socket(AF_INET, SOCK_STREAM, 0);
    if (socketStream == -1)
    {
        printf("[Client] Failed while creating socket steam, with error: %d.\n", errno);
        return 0;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // localhost
    addr.sin_port = htons(3000);

    int connectResult = connect(socketStream, (struct sockaddr *)&addr, sizeof(addr));
    if (connectResult == -1)
    {
        printf("[Client] Connection failed, with error: %d.\n", errno);
        return 0;
    }

    int havePermission = 1;
    int loopingFlag = 1;
    int loggedIn = 0;

    char receivedData[256] = {0};
    char input[100] = {0};

    while (1)
    {
        if (loopingFlag)
        {
            receiveMessage(socketStream, receivedData);
            printf("%s\n", receivedData);
        }

        fgets(input, 30, stdin);
        sendMessage(socketStream, input);
        input[strcspn(input, "\n")] = 0; // 去除 "\n"

        if (strcmp(input, "exit") == 0)
        { // 結束連線
            printf("%s\n", DISCONNECT_MESSAGE);
            break;
        }
        else
        {
            if (loggedIn)
            {
                char *command = strtok(input, " ");

                if (strcmp(command, "read") == 0)
                {
                    // 詢問 remote 檔案的權限
                    receiveMessage(socketStream, receivedData);
                    havePermission = atoi(receivedData);

                    if (havePermission == 0)
                    { // remote 檔案無權限讀取
                        printf("%s\n", PERMISSION_DENIED_MESSAGE);
                        loopingFlag = 0;
                        continue;
                    }
                    else if (havePermission == 2)
                    { // remote 檔案不存在
                        printf("%s\n", FILE_DOES_NOT_EXIST_MESSAGE);
                        loopingFlag = 0;
                        continue;
                    }
                    else
                    {
                        loopingFlag = 1;
                    }

                    // 詢問 remote 檔案的狀態
                    receiveMessage(socketStream, receivedData);
                    int fileStatus = atoi(receivedData);

                    if (fileStatus != 0)
                    { // 遠端檔案暫時不可用
                        printf("%s\n", REMOTE_FILE_IS_BUSY_MESSAGE);
                        loopingFlag = 0;
                        continue;
                    }

                    char buffer[256] = {0};
                    char *fileName = strtok(NULL, " \t\n");
                    FILE *savedFile;
                    savedFile = fopen(fileName, "w");

                    receiveMessage(socketStream, receivedData);
                    int fileSize = atoi(receivedData);

                    int receivedFileSize = 0;

                    // 進行檔案接收
                    while (receivedFileSize < fileSize)
                    {
                        int byteCount = recv(socketStream, buffer, sizeof(buffer), 0);

                        if (byteCount <= 0)
                        {
                            break;
                        }

                        fprintf(savedFile, "%s", buffer);
                        receivedFileSize += strlen(buffer);
                        bzero(buffer, 256);
                    }

                    fclose(savedFile);
                }
                else if (strcmp(command, "write") == 0)
                {
                    char *fileName = strtok(NULL, " \t\n");
                    FILE *localFile;
                    localFile = fopen(fileName, "r");

                    // 確認 local 檔案是否存在
                    // struct stat fileStatus;
                    // if(stat(fileName, &fileStatus) == 0) {
                    //     havePermission = 0;
                    //     continue;
                    // }

                    // 詢問 remote 檔案的權限
                    receiveMessage(socketStream, receivedData);
                    havePermission = atoi(receivedData);

                    if (havePermission == 0)
                    { // remote 檔案無權限讀取
                        printf("%s\n", PERMISSION_DENIED_MESSAGE);
                        loopingFlag = 0;
                        continue;
                    }
                    else if (havePermission == 2)
                    { // remote 檔案不存在
                        printf("%s\n", REMOTE_FILE_DOES_NOT_EXIST_MESSAGE);
                        loopingFlag = 0;
                        continue;
                    }
                    else
                    {
                        loopingFlag = 1;
                    }

                    // 詢問 remote 檔案的狀態
                    receiveMessage(socketStream, receivedData);
                    int fileStatus = atoi(receivedData);

                    if (fileStatus != 0)
                    { // remote 檔案暫時不可用
                        printf("%s\n", REMOTE_FILE_IS_BUSY_MESSAGE);
                        loopingFlag = 0;
                        continue;
                    }

                    char buffer[256] = {0};

                    fseek(localFile, 0L, SEEK_END);
                    int fileSize = ftell(localFile);
                    char fileSizeString[10] = {0};
                    sprintf(fileSizeString, "%d", fileSize);
                    strcat(fileSizeString, "\n");
                    sendMessage(socketStream, fileSizeString);
                    fseek(localFile, 0L, SEEK_SET);

                    // 進行檔案傳送
                    while (fgets(buffer, 256, localFile))
                    {
                        int sentBytes = send(socketStream, buffer, sizeof(buffer), 0);
                        if (sentBytes <= 0)
                        {
                            break;
                        }

                        sleep(1);
                    }

                    fclose(localFile);

                    sleep(1);
                }
                else
                {
                    loopingFlag = 1;
                }
            }
            else
            { // 嘗試登入
                loopingFlag = 0;
                receiveMessage(socketStream, receivedData);
                loggedIn = atoi(receivedData);

                if (loggedIn)
                {
                    printf("%s\n", LOGIN_SUCCESSED_MESSAGE);
                }
                else
                {
                    printf("%s\n", LOGIN_FAILED_MESSAGE);
                }
            }
        }
    }

    close(socketStream);

    return 0;
}
