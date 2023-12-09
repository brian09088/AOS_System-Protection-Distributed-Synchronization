#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

#define WELCOME_MESSAGE "[Server] Welcome, enter your username.\n"
#define COMMAND_DOES_NOT_EXIST "[Server] Command doesn't exist.\n"
#define CREATE_FILE_SUCCESSED_MESSAGE "[Server] File created successfully.\n"
#define CREATE_FILE_FAILED_MESSAGE "[Server] File created failed.\n"
#define FILE_ALREADY_EXISTED_MESSAGE "[Server] File already existed.\n"
#define SEND_SUCCESSED_MESSAGE "[Server] File sent successfully.\n"
#define SEND_FAILED_MESSAGE "[Server] File sent failed.\n"
#define UPDATE_SUCCESSED_MESSAGE "[Server] File updated successfully.\n"
#define UPDATE_FAILED_MESSAGE "[Server] File updated failed.\n"
#define PERMISSION_EDITING_SUCCESSED_MESSAGE "[Server] Changemode done.\n"
#define PERMISSION_EDITING_FAILED_MESSAGE "[Server] Changemode Failed.\n"
#define SERVER_PONG "[Server] Pong!\n"

#define MAX_FILE_COUNT 256

pthread_mutex_t changemodeMutex = PTHREAD_MUTEX_INITIALIZER;

struct fileStatus
{
    char fileName[256];
    int isReading;
    int isWriting;
};

struct connectedClient
{
    int socket;
    struct fileStatus *fileStatuses;
};

/*********************************
*   根據 Username 取得 User ID。
*********************************/
int getUserID(char *userName)
{
    FILE *passwd;
    char buffer[256];
    int userID = 999;

    passwd = fopen("passwd.txt", "r");

    while (fgets(buffer, 256, passwd))
    {
        buffer[strcspn(buffer, "\n")] = 0;
        char *checkingUser = strtok(buffer, ":");

        if (strcmp(userName, checkingUser) == 0)
        {
            checkingUser = strtok(NULL, ":");
            userID = atoi(checkingUser);
        }
    }

    fclose(passwd);
    return userID;
}
/*********************************
*   根據 User ID 取得 Group ID。
*********************************/
int getGroupID(int userID)
{
    FILE *passwd;
    char buffer[256];
    int groupID = 999;

    passwd = fopen("passwd.txt", "r");

    while (fgets(buffer, 256, passwd))
    {
        buffer[strcspn(buffer, "\n")] = 0;
        char *checkingUser = strtok(buffer, ":");
        checkingUser = strtok(NULL, ":");
        int checkingUserID = atoi(checkingUser);

        if (userID == checkingUserID)
        {
            checkingUser = strtok(NULL, ":");
            groupID = atoi(checkingUser);
        }
    }

    fclose(passwd);
    return groupID;
}

/****************************
*   編輯 Capability List。
****************************/
int editPermission(char *fileName, int userID, char *newPermission)
{
    FILE *capabilityList;
    FILE *capabilityListTemp;
    char buffer[256] = {0};
    char ownerPermission[2] = {0};
    char groupPermission[2] = {0};
    char otherPermission[2] = {0};

    pthread_mutex_lock(&changemodeMutex); // 防止衝突

    capabilityList = fopen("capability-list.txt", "r");
    capabilityListTemp = fopen(".capability-list.txt.temp", "w");

    // 取得權限字串 (rwrwrw)
    strncpy(ownerPermission, newPermission, 2);
    strncpy(groupPermission, newPermission + 2, 2);
    strncpy(otherPermission, newPermission + 4, 2);

    while (fgets(buffer, 256, capabilityList))
    {
        char *editingFile = strtok(buffer, ":");

        int isTheTargetFile = 0;
        if (strcmp(editingFile, fileName) == 0)
        {
            isTheTargetFile = 1;
        }

        char *selectedUser = strtok(NULL, ":");

        char stringTemp[256] = {0};
        strcat(stringTemp, editingFile);
        strcat(stringTemp, ":");
        strcat(stringTemp, selectedUser);
        strcat(stringTemp, ":");

        if (isTheTargetFile)
        {
            int selectedUserID = atoi(selectedUser);
            int inTheSameGroup = 0;

            if (getGroupID(userID) == getGroupID(selectedUserID) && userID != selectedUserID)
            { // 在同個 group
                inTheSameGroup = 1;
                strncat(stringTemp, groupPermission, 2);
            }
            
            if (userID == selectedUserID)
            { // 爲 owner
                strncat(stringTemp, ownerPermission, 2);
            }

            if (!inTheSameGroup && userID != selectedUserID)
            { // 其他 user
                strncat(stringTemp, otherPermission, 2);
            }
        }
        else
        {
            char *permission = strtok(NULL, " \n\t");
            strncat(stringTemp, permission, 2);
        }

        // 處理輸出至 file stream 和換行
        fprintf(capabilityListTemp, "%s", stringTemp);
        fprintf(capabilityListTemp, "%s", "\n");
    }

    fclose(capabilityList);
    fclose(capabilityListTemp);
    remove("capability-list.txt");
    rename(".capability-list.txt.temp", "capability-list.txt");

    pthread_mutex_unlock(&changemodeMutex);

    return 0;
}

/***************************************
*   以 Capability List 檢查檔案的權限。
***************************************/
int checkPermission(char *fileName, int userID)
{
    FILE *capabilityList;
    char buffer[256] = {0};
    int result = 4;

    capabilityList = fopen("capability-list.txt", "r");

    while (fgets(buffer, 256, capabilityList))
    {
        buffer[strcspn(buffer, "\n")] = 0;
        char *checkingFile = strtok(buffer, ":");

        if (strcmp(fileName, checkingFile) == 0)
        { // 爲目前指定檔案
            checkingFile = strtok(NULL, ":");
            int checkingUserID = atoi(checkingFile);

            if (userID == checkingUserID)
            {
                char *permission = strtok(NULL, ":");
                if (strcmp(permission, "r-") == 0)
                { // 只有 read
                    result = 1;
                }
                else if (strcmp(permission, "-w") == 0)
                { // 只有 write
                    result = 2;
                }
                else if (strcmp(permission, "rw") == 0)
                { // read 和 write
                    result = 3;
                }
                else
                { // 無權限
                    result = 0;
                }
            }
        }
    }

    fclose(capabilityList);

    return result;
}

/********************************************
*   以 persistent 的 Capability List 檔案，
*   建立檔案資訊 struct。
********************************************/
void updateFileStatuses(struct fileStatus *fileStatuses)
{
    FILE *capabilityList;
    char buffer[256] = {0};
    int index = 0; 

    capabilityList = fopen("capability-list.txt", "r");

    fgets(buffer, 256, capabilityList);
    char *checkingFile = strtok(buffer, ":");
    char doneFile[256] = {0};
    strcpy(doneFile, checkingFile);
    fseek(capabilityList, 0, SEEK_SET);

    while (fgets(buffer, 256, capabilityList))
    {
        char *checkingFileTemp = strtok(buffer, ":");

        if (index != 0)
        {
            if (strcmp(checkingFileTemp, doneFile) == 0)
            { // 忽略重複檔名
                continue;
            }
            else
            {
                strcpy(doneFile, checkingFileTemp);
            }
        }

        strcpy(fileStatuses[index].fileName, doneFile);
        fileStatuses[index].isReading = 0;
        fileStatuses[index].isWriting = 0;

        index++;
    }

    fclose(capabilityList);
}

/**************************
*   檢查檔案的可用性，
*   回傳是否正在進行讀寫。
**************************/
int checkFileAvailability(struct fileStatus *serverFileStatuses, char *fileName)
{
    int result = 0;
    struct fileStatus *targetFileStatus;

    // 取得目前檔案資訊
    for (int i = 0; i < MAX_FILE_COUNT; i++)
    {
        if (strcmp(serverFileStatuses[i].fileName, fileName) == 0)
        {
            targetFileStatus = &(serverFileStatuses[i]);
            break;
        }
    }

    if (targetFileStatus->isReading)
    { // 正在讀取
        result = 1;
    }
    else if (targetFileStatus->isWriting)
    { // 正在寫入
        result = 2;
    }

    return result;
}

/*******************************************
*   以 persistent 的 Passwd 檔案登入使用者。
*******************************************/
int login(char *userName)
{
    FILE *passwd;
    char buffer[256] = {0};
    int result = 0;

    passwd = fopen("passwd.txt", "r");

    while (fgets(buffer, 256, passwd))
    {
        buffer[strcspn(buffer, "\n")] = 0;
        char strPtr;
        char *checkingUserName = strtok(buffer, ":");

        if (strcmp(userName, checkingUserName) == 0)
        { // 若使用者存在則登入
            result = 1;
        }
    }

    fclose(passwd);
    return result;
}

/***********************************
*   傳送訊息，
*   並確認傳輸出去的長度等於訊息長度。
***********************************/
void sendMessage(int *socketStream, char *sendingMessage)
{
    int sentLength = 0;
    int totalLength = 0;
    int messageLength = strlen(sendingMessage) + 1;

    while (totalLength != messageLength)
    {
        sentLength = send(*socketStream, &sendingMessage[totalLength], strlen(sendingMessage) + 1 - totalLength, 0);
        totalLength += sentLength;
    }

    sleep(1);
}

/*****************************
*   接收訊息，
*   以句尾 "\n" 當作接收完成。    
*****************************/
void receiveMessage(int *socketStream, char *receivedMessage)
{
    int notFound = 1;
    int receivedLength = 0;
    int totalLength = 0;
    char buffer[256] = {0};

    bzero(receivedMessage, 256); // 初始化

    while (notFound)
    {
        receivedLength = recv(*socketStream, &buffer[totalLength], sizeof(buffer) - totalLength - 1, 0);
        totalLength += receivedLength;
        buffer[totalLength] = '\0';
        if (strchr(buffer, '\n') != 0)
        { // 存在 "\n" 則停止
            notFound = 0;
        }
    }

    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(receivedMessage, buffer);
}

/******************************
*   在 Server 端建立空檔案，
*   並修改 Capability List。
******************************/
int createFile(int userID, struct fileStatus *serverFileStatuses, char *fileName, char *permission)
{
    FILE *newFile;
    FILE *passwd;
    FILE *capabilityList;
    char filePath[512] = "./Files/";

    // 檢查檔案是否存在（僅檢查 Capability List）
    if (checkPermission(fileName, userID) != 4)
    {
        return 2;
    }

    strcat(filePath, fileName);

    // 建立空檔案
    newFile = fopen(filePath, "w");
    fclose(newFile);

    passwd = fopen("passwd.txt", "r");
    if (passwd == NULL)
    {
        return 1;
    }

    capabilityList = fopen("capability-list.txt", "a"); // append
    if (capabilityList == NULL)
    {
        return 1;
    }

    char buffer[256];
    int userIDs[256];
    int index = 0;

    // 取得所有存在的 User IDs
    while (fgets(buffer, 256, passwd))
    {
        buffer[strcspn(buffer, "\n")] = 0;
        char *checkingUser = strtok(buffer, ":");
        checkingUser = strtok(NULL, ":");
        int userID = atoi(checkingUser);

        userIDs[index] = userID;
        index++;
    }

    // 寫入 Capabiliy List，爲每個使用者添加該資料的資訊，預設為 "------"
    for (int i = 0; i < index; i++)
    {
        char stringTemp[256] = {0};
        strcat(stringTemp, fileName);
        strcat(stringTemp, ":");

        char userIDString[5];
        sprintf(userIDString, "%d", userIDs[i]);
        strcat(stringTemp, userIDString);
        strcat(stringTemp, ":--");

        fprintf(capabilityList, "%s", stringTemp);
        fprintf(capabilityList, "%s", "\n");
    }

    fclose(passwd);
    fclose(capabilityList);

    editPermission(fileName, userID, permission); // 根據指令改變此檔案的權限
    updateFileStatuses(serverFileStatuses); // 根據新的 capability list 更新檔案狀態 struct

    return 0;
}

/**************************
*   傳送檔案，
*   直到讀取至最後一行為止。
***************************/
int sendFile(int *client, struct fileStatus *serverFileStatuses, char *fileName)
{
    FILE *openedFile;
    char filePath[256] = {0};
    char buffer[256] = {0};
    struct fileStatus *targetFileStatus;

    // 組成檔案路徑
    strcat(filePath, "./Files/");
    strcat(filePath, fileName);

    // 取得目前檔案資訊
    for (int i = 0; i < MAX_FILE_COUNT; i++)
    {
        if (strcmp(serverFileStatuses[i].fileName, fileName) == 0)
        {
            targetFileStatus = &(serverFileStatuses[i]);
            break;
        }
    }

    targetFileStatus->isReading = 1; // 標示正在讀取

    openedFile = fopen(filePath, "r");
    if (openedFile == NULL)
    {
        return 1;
    }

    // 取得並傳送檔案長度
    fseek(openedFile, 0L, SEEK_END);
    int fileSize = ftell(openedFile);
    char fileSizeString[10] = {0};
    sprintf(fileSizeString, "%d", fileSize);
    strcat(fileSizeString, "\n");
    sendMessage(client, fileSizeString);
    fseek(openedFile, 0L, SEEK_SET);

    // 持續接收直到最後一行
    while (fgets(buffer, 256, openedFile))
    {
        int sentBytes = send(*client, buffer, sizeof(buffer), 0);
        if (sentBytes == -1)
        {
            return 1;
        }

        sleep(1);
    }

    fclose(openedFile);

    targetFileStatus->isReading = 0; // 標示讀取結束

    return 0;
}

/*********************************
*   接收檔案，
*   直到接受到完全檔案長度完成為止。    
*********************************/
int receiveFile(int *client, struct fileStatus *serverFileStatuses, char *fileName, int mode)
{
    FILE *receivedFile;
    char buffer[256];
    char filePath[256] = {0};
    int receivedFileSize = 0;
    struct fileStatus *targetFileStatus;

    strcat(filePath, "./Files/");
    strcat(filePath, fileName);

    // 取得目前檔案資訊
    for (int i = 0; i < MAX_FILE_COUNT; i++)
    {
        if (strcmp(serverFileStatuses[i].fileName, fileName) == 0)
        {
            targetFileStatus = &(serverFileStatuses[i]);
            break;
        }
    }

    targetFileStatus->isWriting = 1; // 標示正在寫入

    if (mode == 1)
    { // 檔案附加 (tail)
        receivedFile = fopen(filePath, "a");
    }
    else
    { // 檔案覆寫
        receivedFile = fopen(filePath, "w");
    }

    if (receivedFile == NULL)
    {
    	perror("Error opening file");
        return 1;
    }

    // 從 client 端取得 remote 檔案大小
    receiveMessage(client, buffer);
    int fileSize = atoi(buffer);

    // 持續接收直到指定長度
    while (receivedFileSize < fileSize)
    {
        int byteCount = recv(*client, buffer, sizeof(buffer), 0);

        if (byteCount <= 0)
        {
            return 1;
        }
	// Print received data  
    	printf("Received: %s\n", buffer);
	
        fprintf(receivedFile, "%s", buffer);
        receivedFileSize += strlen(buffer);
        bzero(buffer, 256);
    }

    fflush(receivedFile);
    fclose(receivedFile);

    targetFileStatus->isWriting = 0; // 標示寫入完成
    
    return 0;
}

/*****************************************
*   主要的服務 function，
*   需要傳入 socket 資訊和檔案資訊 struct。
*****************************************/
void *service(void *clientPtr)
{
    struct connectedClient *clientArg = (struct connectedClient *)clientPtr;
    int *client = &(clientArg->socket);
    struct fileStatus *serverFileStatuses = clientArg->fileStatuses;

    char received[256] = {0};
    int loggedIn = 0;  // 登入 flag
    int userID = 999;  // 使用者 ID
    int groupID = 999; // 使用者 Group

    sendMessage(client, WELCOME_MESSAGE);

    while (1)
    {
        receiveMessage(client, received);
        if (strcmp(received, "exit") == 0)
        { // 收到 exit 則停止
            break;
        }

        if (loggedIn)
        { // 如果為登入狀態，進行服務
            char *command = strtok(received, " ");

            if (strcmp(command, "create") == 0)
            {
                char *fileName = strtok(NULL, " ");
                char *permission = strtok(NULL, " \t\n");

                int createdResult = createFile(userID, serverFileStatuses, fileName, permission);
                if (createdResult == 0)
                { // create 成功
                    sendMessage(client, CREATE_FILE_SUCCESSED_MESSAGE);
                }
                else if (createdResult == 2)
                { // 檔案已存在
                    sendMessage(client, FILE_ALREADY_EXISTED_MESSAGE);
                }
                else
                { // create 失敗
                    sendMessage(client, CREATE_FILE_FAILED_MESSAGE);
                }
            }
            else if (strcmp(command, "read") == 0)
            {
                char *fileName = strtok(NULL, " \t\n");
                char havePermission[3] = {0};

                if (fileName == NULL)
                { // 使用者沒有輸入檔名
                    strcpy(havePermission, "2\n");
                    sendMessage(client, havePermission);
                }

                int permission = checkPermission(fileName, userID);

                if (permission == 1 || permission == 3)
                { // 使用者有讀取權限 (r, rw)
                    strcpy(havePermission, "1\n");
                    sendMessage(client, havePermission);

                    int fileStatus = checkFileAvailability(serverFileStatuses, fileName);
                    char currentFileStatus[3] = {0};
                    if (fileStatus == 2)
                    { // 檔案正在寫入
                        strcpy(currentFileStatus, "1\n");
                        sendMessage(client, currentFileStatus);
                        continue;
                    }
                    else
                    { // 檔案可用
                        strcpy(currentFileStatus, "0\n");
                        sendMessage(client, currentFileStatus);
                    }

                    int sentResult = sendFile(client, serverFileStatuses, fileName);
                    if (sentResult == 0)
                    { // 檔案傳輸成功
                        sendMessage(client, SEND_SUCCESSED_MESSAGE);
                    }
                    else if (sentResult == 1)
                    { // 檔案傳輸失敗
                        sendMessage(client, SEND_FAILED_MESSAGE);
                    }
                }
                else if (permission == 4)
                { // 檔案不存在
                    strcpy(havePermission, "2\n");
                    sendMessage(client, havePermission);
                }
                else
                { // 使用者沒有讀取權限
                    strcpy(havePermission, "0\n");
                    sendMessage(client, havePermission);
                }
            }
            else if (strcmp(command, "write") == 0)
            {
                char *fileName = strtok(NULL, " ");
                char *mode = strtok(NULL, " \t\n");

                int permission = checkPermission(fileName, userID);
                char havePermission[3] = {0};

                if (permission == 2 || permission == 3)
                { // 使用者有寫入權限 (w, rw)
                    strcpy(havePermission, "1\n");
                    sendMessage(client, havePermission);

                    int fileStatus = checkFileAvailability(serverFileStatuses, fileName);
                    char currentFileStatus[3] = {0};
                    if (fileStatus == 1 || fileStatus == 2)
                    { // 檔案正在讀取或寫入
                        strcpy(currentFileStatus, "1\n");
                        sendMessage(client, currentFileStatus);
                        continue;
                    }
                    else
                    { // 檔案可用
                        strcpy(currentFileStatus, "0\n");
                        sendMessage(client, currentFileStatus);
                    }

                    int isAppending = 0;
                    if (strcmp(mode, "a") == 0)
                    { // 若指令為附加 (append)
                        isAppending = 1;
                    }

                    int receivedResult = receiveFile(client, serverFileStatuses, fileName, isAppending);
                    if (receivedResult == 0)
                    { // 檔案更新成功
                        sendMessage(client, UPDATE_SUCCESSED_MESSAGE);
                    }
                    else if (receivedResult == 1)
                    { // 檔案更新失敗
                        sendMessage(client, UPDATE_FAILED_MESSAGE);
                    }
                }
                else if (permission == 4)
                { // 檔案不存在
                    strcpy(havePermission, "2\n");
                    sendMessage(client, havePermission);
                }
                else
                { // 使用者沒有寫入權限
                    strcpy(havePermission, "0\n");
                    sendMessage(client, havePermission);
                }
            }
            else if (strcmp(command, "changemode") == 0)
            {
                char *fileName = strtok(NULL, " ");
                char *newPermission = strtok(NULL, " \t\n");

                int editedResult = editPermission(fileName, userID, newPermission);

                if (editedResult == 0)
                { // changemode 成功
                    sendMessage(client, PERMISSION_EDITING_SUCCESSED_MESSAGE);
                }
                else
                { // changemode 失敗
                    sendMessage(client, PERMISSION_EDITING_FAILED_MESSAGE);
                }
            }
            else if (strcmp(command, "ping") == 0)
            { // Ping-pong!
                sendMessage(client, SERVER_PONG);
            }
            else
            { // 指令不存在
                sendMessage(client, COMMAND_DOES_NOT_EXIST);
            }
        }
        else
        {
            char isLoggedIn[3] = {0};

            if (login(received)) 
            { // 成功登入
                loggedIn = 1;
                userID = getUserID(received);
                groupID = getGroupID(userID);

                strcpy(isLoggedIn, "1\n");
                sendMessage(client, isLoggedIn);
            }
            else 
            { // 登入失敗
                strcpy(isLoggedIn, "0\n");
                sendMessage(client, isLoggedIn);
            }
        }
    }

    close(*client);

    pthread_exit(NULL);
    return NULL;
}

int main()
{
    pthread_t clientThreads[10]; // 上限 10 人
    int clientCount = 0;

    /*********************
    *   初始化 socket。    
    *********************/

    // int socket(int domain, int type, int protocol);
    int socketStream = socket(AF_INET, SOCK_STREAM, 0); // IP data 的 socket stream
    if (socketStream == -1)
    {
        printf("[System] Failed while creating socket stream, with error: %d.\n", errno);
        return 0;
    }

    int enabled = 1;
    setsockopt(socketStream, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    struct sockaddr_in addr = {0};            // Internet 位址格式
    addr.sin_family = AF_INET;                // 使用 IPv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // listen 0.0.0.0，只 listen 指定 port
    addr.sin_port = htons(3000);              // 指定 port number

    // int bind(int sockfd, struct sockaddr* addr, int addrlen);
    int bindingResult = bind(socketStream, (struct sockaddr *)&addr, sizeof(addr)); // bind 自己的位址，當做 Server
    if (bindingResult == -1)
    {
        printf("[System] Socket binding failed, with error: %d.\n", errno);
        return 0;
    }

    // int listen(int sockfd, int backlog);
    int listenResult = listen(socketStream, 10); // listening, 作業指定最大後備連接數爲 10
    if (listenResult == -1)
    {
        printf("[System] Listening failed, with error: %d.\n", errno);
    }

    struct fileStatus handingFileStatuses[MAX_FILE_COUNT];
    struct connectedClient clients[10];

    updateFileStatuses(handingFileStatuses); // 初始化資料狀態 struct

    /*****************************************
    *   接受新的連線請求，並放入新的 thread 中，
    *   共用的資料狀態 struct 傳入 argument。
    *****************************************/

    // int accept(int sockfd, struct sockaddr addr, socklen_t addrlen);
    while (1)
    {
        struct sockaddr_in clientAddr;
        int clientAddrLength = sizeof(clientAddr);

        clients[clientCount].socket = accept(socketStream, (struct sockaddr *)&clientAddr, &clientAddrLength);
        if (clients[clientCount].socket == -1)
        {
            printf("[System] Failed while accepting new client, with error: %d.\n", errno);
            continue;
        }

        clients[clientCount].fileStatuses = handingFileStatuses; // 填入初始化的資料 struct

        char clientAddress[INET_ADDRSTRLEN] = {0};

        
        getpeername(socketStream, (struct sockaddr *)&clientAddr, &clientAddrLength);   // 取得 client 的資訊
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientAddress, sizeof(clientAddress)); // 取得 IP address
        int clientPort = ntohs(clientAddr.sin_port);                                    // 取得 Port number

        pthread_create(&clientThreads[clientCount], NULL, service, (void *)&clients[clientCount]);
        printf("[System] Thread %d created. Client %s:%d.\n", clientCount, clientAddress, clientPort);

        pthread_detach(clientThreads[clientCount++]); // 保持 main thread
    }

    close(socketStream);
}
