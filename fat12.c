/*文件名不支持中文*/
// 没完成的工作：扩展普通目录项。默认普通目录项占不满一个扇区
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_FILE 0
#define ENTRY_DIR 1

typedef unsigned char u8;   //1字节
typedef unsigned short u16; //2字节
typedef unsigned int u32;   //4字节

int BytsPerSec; //每扇区字节数
int SecPerClus; //每簇扇区数
int RsvdSecCnt; //Boot记录占用的扇区数
int NumFATs;    //FAT表个数
int RootEntCnt; //根目录最大文件数
int FATSz;      //FAT扇区数

#pragma pack(1) /*指定按1字节对齐*/

//偏移0个字节
struct BPB
{
    u8 JmpCode[3];
    u8 BS_OEMName[8];
    u16 BPB_BytsPerSec; //每扇区字节数
    u8 BPB_SecPerClus;  //每簇扇区数
    u16 BPB_RsvdSecCnt; //Boot记录占用的扇区数
    u8 BPB_NumFATs;     //FAT表个数
    u16 BPB_RootEntCnt; //根目录最大文件数
    u16 BPB_TotSec16;
    u8 BPB_Media;
    u16 BPB_FATSz16; //FAT扇区数
    u16 BPB_SecPerTrk;
    u16 BPB_NumHeads;
    u32 BPB_HiddSec;
    u32 BPB_TotSec32; //如果BPB_FATSz16为0，该值为FAT扇区数
} __attribute__((packed));
//BPB至此结束，长度36字节

// 根目录条目
struct RootEntry
{
    char DIR_Name[11];
    u8 DIR_Attr; //文件属性
    char reserved[10];
    u16 DIR_WrtTime;
    u16 DIR_WrtDate;
    u16 DIR_FstClus; //开始簇号
    u32 DIR_FileSize;
} __attribute__((packed));
// 根目录条目结束，32字节

// 子目录条目
struct FileEntry
{
    char DIR_Name[11];
    u8 DIR_Attr; //文件属性
    char reserved[10];
    u16 DIR_WrtTime;
    u16 DIR_WrtDate;
    u16 DIR_FstClus; //开始簇号
    u32 DIR_FileSize;
};
// 子目录条目结束，32字节

// 建立索引目录条目
struct FileList
{
    char full_Name[256];
    // char *full_Name;
    u8 DIR_Attr;
    u16 DIR_FstClus;
    u32 DIR_FileSize;
    struct FileList *father;
};
struct FileList *rootFileList[128];
int rootFileCnt = -1;

#pragma pack() /*取消指定对齐，恢复缺省对齐*/

int freeSector[2880];

void fillBPB(FILE *fat12, struct BPB *bpb_ptr);                                                    //载入BPB
void printFiles(FILE *fat12, struct RootEntry *rootEntry_ptr);                                     //打印文件名，这个函数在打印目录时会调用下面的printChildren
void printChildren(FILE *fat12, char *directory, int startClus, struct FileList *currentFileList); //打印目录及目录下子文件名
int getFATValue(FILE *fat12, int num);                                                             //读取num号FAT项所在的两个字节，并从这两个连续字节中取出FAT项的值，
int readImg(FILE *fat12, char *directory);
int matchEntry(char *directory);
int checkFree(FILE *fat12);
int existsEntry(char *directory);
int writeImg(FILE *fat12, char *directory, struct RootEntry *rootEntry_ptr);
int createRootEntry(FILE *fat12, char *directory, int type);
int createSubEntry(FILE *fat12, char *processedPath, char *remainPath, char *directory, int preEntry);
int seekEmptyCluster();
int writeFATValue(FILE *fat12, int num, int next);

char fatPATH[128];
char sysPATH[128];
char imgPATH[128];

#include <ctype.h>
void toUpper(char *orign)
{
    char *str = orign;
    for (; *str != '\0'; str++)
    {
        *str = toupper(*str);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("fat12 -f img.bin                    \n// 创建并格式化磁盘镜像文件img.bin\n\n\
fat12 -mi img.bin /demo/ fileA.txt  \n// 将宿主文件系统当前目录下文件fileA.txt拷贝到磁盘镜像img.bin的/demo目录中\n\n\
fat12 -mo img.bin /demo/fileB.txt . \n// 将磁盘镜像img.bin中/demo/fileB.txt文件拷贝到宿主文件系统当前目录\n\n");
        return 0;
    }

    memset(imgPATH, 0, 128);
    memset(fatPATH, 0, 128);
    memset(sysPATH, 0, 128);

    if (strcmp(argv[1], "-f") == 0)
    {
        // 创建并格式化磁盘镜像文件
        printf("f\n");

        strcpy(imgPATH, argv[2]);

        FILE *fat12;
        fat12 = fopen(imgPATH, "w+b"); //新建FAT12映像文件
        struct BPB *bpb;
        bpb = (struct BPB *)malloc(sizeof(struct BPB));
        char *newFAT = (char *)malloc(sizeof(char) * 1474560);
        memset(newFAT, 0, 1474560);
        fseek(fat12, 0, SEEK_SET);
        fwrite(newFAT, 1, 1474560, fat12);
        fclose(fat12);

        // return 0;

        fat12 = fopen(imgPATH, "r+b");
        fseek(fat12, 0, SEEK_SET);
        fread(bpb, 1, sizeof(struct BPB), fat12);
        memcpy(bpb->BS_OEMName, "StellarD", 8);
        bpb->JmpCode[0] = 0xeb; // jmp
        bpb->JmpCode[1] = 0x58; // 0x58
        bpb->JmpCode[2] = 0x90; // nop(大概是对齐用)
        bpb->BPB_BytsPerSec = 0x200;
        bpb->BPB_FATSz16 = 0x9;
        bpb->BPB_HiddSec = 0x0;
        bpb->BPB_Media = 0xf0;
        bpb->BPB_NumFATs = 0x2;
        bpb->BPB_NumHeads = 0x2;
        bpb->BPB_RootEntCnt = 0xE0;
        bpb->BPB_RsvdSecCnt = 0x1; // 其实可为0
        bpb->BPB_SecPerClus = 0x1;
        bpb->BPB_SecPerTrk = 0x12;
        bpb->BPB_TotSec16 = 0xb40;
        bpb->BPB_TotSec32 = 0;
        fseek(fat12, 0, SEEK_SET);
        fwrite(bpb, 1, sizeof(struct BPB), fat12);

        fseek(fat12, 510, SEEK_SET);
        u16 fat12Code = 0xaa55;
        fwrite(&fat12Code, 2, 1, fat12);
        fclose(fat12);

        fat12 = fopen(imgPATH, "r+b"); //打开FAT12的映像文件
        //初始化各个全局变量
        BytsPerSec = bpb->BPB_BytsPerSec;
        SecPerClus = bpb->BPB_SecPerClus;
        RsvdSecCnt = bpb->BPB_RsvdSecCnt;
        NumFATs = bpb->BPB_NumFATs;
        RootEntCnt = bpb->BPB_RootEntCnt;
        if (bpb->BPB_FATSz16 != 0)
        {
            FATSz = bpb->BPB_FATSz16;
        }
        else
        {
            FATSz = bpb->BPB_TotSec32;
        }
        writeFATValue(fat12, 0, 0xff0);
        writeFATValue(fat12, 1, 0xfff);
        fclose(fat12);

        return 0;
    }
    else if (strcmp(argv[1], "-mi") == 0)
    {
        // 写入fat12
        printf("mi\n");

        strcpy(imgPATH, argv[2]);
        strcpy(fatPATH, argv[3]);
        strcpy(sysPATH, argv[4]);

        if (fatPATH[0] == '.')
        {
            fatPATH[0] = '\0';
        }
        if (fatPATH[0] == '/')
        {
            char temp[128];
            strcpy(temp, &fatPATH[1]);
            strcpy(fatPATH, temp);
        }

        int lenSys = strlen(sysPATH), i;
        for (i = lenSys - 1; i >= 0; i--)
        {
            if (sysPATH[i] == '/')
            {
                break;
            }
        }
        i++;
        if (i >= 0)
        {
            strcat(fatPATH, &sysPATH[i]);
        }
        else
        {
            strcat(fatPATH, sysPATH);
        }

        toUpper(fatPATH);
        printf("\e[0;36mimgPATH = \e[0m\e[1;36m%s\e[0m\n", imgPATH);
        printf("\e[0;36msysPATH = \e[0m\e[1;36m%s\e[0m\n", sysPATH);
        printf("\e[0;36mfatPATH = \e[0m\e[1;36m%s\e[0m\n", fatPATH);

        FILE *fp = fopen(imgPATH, "rb");
        if (!fp)
        {
            printf("image file not exist\n");
            return -1;
        }
        fseek(fp, 0L, SEEK_END);
        printf("Size of image = %ld\n", ftell(fp));
        fclose(fp);

        fp = fopen(sysPATH, "rb");
        if (!fp)
        {
            printf("the targeted file not exist\n");
            return -1;
        }
        fseek(fp, 0L, SEEK_END);
        int targetSize = ftell(fp);
        printf("Size of target = %d\n", targetSize);
        fclose(fp);
        // return 0;
        // 正篇开始，读取img
        FILE *fat12;
        fat12 = fopen(imgPATH, "rb"); //打开FAT12的映像文件

        struct BPB bpb;
        struct BPB *bpb_ptr = &bpb;

        //载入BPB
        fillBPB(fat12, bpb_ptr);

        //初始化各个全局变量
        BytsPerSec = bpb_ptr->BPB_BytsPerSec;
        SecPerClus = bpb_ptr->BPB_SecPerClus;
        RsvdSecCnt = bpb_ptr->BPB_RsvdSecCnt;
        NumFATs = bpb_ptr->BPB_NumFATs;
        RootEntCnt = bpb_ptr->BPB_RootEntCnt;
        if (bpb_ptr->BPB_FATSz16 != 0)
        {
            FATSz = bpb_ptr->BPB_FATSz16;
        }
        else
        {
            FATSz = bpb_ptr->BPB_TotSec32;
        }

        struct RootEntry rootEntry;
        struct RootEntry *rootEntry_ptr = &rootEntry;

        //打印文件名
        printFiles(fat12, rootEntry_ptr);
        fclose(fat12);

        fat12 = fopen(imgPATH, "r+b");
        int freeSectorNum = checkFree(fat12);
        int freeSpaceBytes = freeSectorNum * BytsPerSec;
        printf("Space Used: %d, Space Avaliable %d (Bytes)\n", (2880 - freeSectorNum) * BytsPerSec, freeSectorNum * BytsPerSec);
        if (freeSpaceBytes <= targetSize)
        {
            printf("Target size: %d, need more free space.\n", targetSize);
            return 0;
        }
        // char train[100] = "ARK/TWITTER/OK.TXT";
        // char train[100] = "OK.TXT";
        // char train[100] = "BRK/OPOER/SYS.TXT";
        // char train[100] = "ARK/BRK/CRK.TXT";
        // char train[100] = "USR/STELL.TXT";
        // char train[100] = "ARK/TWITTER/STELL.TXT";
        // char train[100] = "ARK/BRK/HELLO.TXT";
        char directory[128];
        strcpy(directory, fatPATH);
        writeImg(fat12, directory, rootEntry_ptr);
        freeSectorNum = checkFree(fat12);
        printf("Space Used: %d, Space Avaliable %d (Bytes)\n", (2880 - freeSectorNum) * BytsPerSec, freeSectorNum * BytsPerSec);

        fclose(fat12);

        return 0;
    }
    else if (strcmp(argv[1], "-mo") == 0)
    {
        // 读取fat12
        printf("mo\n");
        strcpy(imgPATH, argv[2]);
        strcpy(fatPATH, argv[3]);
        strcpy(sysPATH, argv[4]);

        int lenFat = strlen(fatPATH), i;
        for (i = lenFat - 1; i >= 0; i--)
        {
            if (fatPATH[i] == '/')
            {
                break;
            }
        }
        if (i >= 0)
        {
            strcat(sysPATH, &fatPATH[i]);
        }
        else
        {
            strcat(sysPATH, "/");
            strcat(sysPATH, fatPATH);
        }

        toUpper(fatPATH);
        if (fatPATH[0] == '/')
        {
            char temp[128];
            strcpy(temp, &fatPATH[1]);
            strcpy(fatPATH, temp);
        }
        printf("imgPATH = %s\n", imgPATH);
        printf("sysPATH = %s\n", sysPATH);
        printf("fatPATH = %s\n", fatPATH);

        FILE *fp = fopen(imgPATH, "rb");
        if (!fp)
        {
            printf("image file not exist\n");
            return -1;
        }
        fseek(fp, 0L, SEEK_END);
        printf("size = %ld\n", ftell(fp));
        fclose(fp);

        // 正篇开始，读取img
        FILE *fat12;
        fat12 = fopen(imgPATH, "rb"); //打开FAT12的映像文件

        struct BPB bpb;
        struct BPB *bpb_ptr = &bpb;

        //载入BPB
        fillBPB(fat12, bpb_ptr);

        //初始化各个全局变量
        BytsPerSec = bpb_ptr->BPB_BytsPerSec;
        SecPerClus = bpb_ptr->BPB_SecPerClus;
        RsvdSecCnt = bpb_ptr->BPB_RsvdSecCnt;
        NumFATs = bpb_ptr->BPB_NumFATs;
        RootEntCnt = bpb_ptr->BPB_RootEntCnt;
        if (bpb_ptr->BPB_FATSz16 != 0)
        {
            FATSz = bpb_ptr->BPB_FATSz16;
        }
        else
        {
            FATSz = bpb_ptr->BPB_TotSec32;
        }

        struct RootEntry rootEntry;
        struct RootEntry *rootEntry_ptr = &rootEntry;

        //打印文件名
        printFiles(fat12, rootEntry_ptr);

        // 读取文件至外部
        // char test[100] = "ARK/ARK1.JPG";
        char test[100] = "ARK/TWITTER/WAAIFU.PNG";
        // char test[100] = "ARK/BRK/HELLO.TXT";
        readImg(fat12, test);

        fclose(fat12);

        return 0;
    }
    else
    {
        // 错误
        printf("fat12 -f img.bin                    \n// 创建并格式化磁盘镜像文件img.bin\n\n\
fat12 -mi img.bin /demo/ fileA.txt  \n// 将宿主文件系统当前目录下文件fileA.txt拷贝到磁盘镜像img.bin的/demo目录中\n\n\
fat12 -mo img.bin /demo/fileB.txt . \n// 将磁盘镜像img.bin中/demo/fileB.txt文件拷贝到宿主文件系统当前目录\n\n");
        return 0;
    }

    // 以下为没启用argc与argv时的测试内容
    FILE *fat12;
    fat12 = fopen("image3.img", "rb"); //打开FAT12的映像文件

    struct BPB bpb;
    struct BPB *bpb_ptr = &bpb;

    //载入BPB
    fillBPB(fat12, bpb_ptr);

    //初始化各个全局变量
    BytsPerSec = bpb_ptr->BPB_BytsPerSec;
    SecPerClus = bpb_ptr->BPB_SecPerClus;
    RsvdSecCnt = bpb_ptr->BPB_RsvdSecCnt;
    NumFATs = bpb_ptr->BPB_NumFATs;
    RootEntCnt = bpb_ptr->BPB_RootEntCnt;
    if (bpb_ptr->BPB_FATSz16 != 0)
    {
        FATSz = bpb_ptr->BPB_FATSz16;
    }
    else
    {
        FATSz = bpb_ptr->BPB_TotSec32;
    }

    struct RootEntry rootEntry;
    struct RootEntry *rootEntry_ptr = &rootEntry;

    //打印文件名
    printFiles(fat12, rootEntry_ptr);

    // checkFree(fat12);

    // 读取文件至外部
    // char test[100] = "ARK/ARK1.JPG";
    // char test[100] = "ARK/TWITTER/WAAIFU.PNG";
    // char test[100] = "ARK/BRK/HELLO.TXT";
    // readImg(fat12, test);

    fclose(fat12);

    fat12 = fopen("image3.img", "r+b");

    checkFree(fat12);

    // char train[100] = "ARK/TWITTER/OK.TXT";
    // char train[100] = "OK.TXT";
    char train[100] = "BRK/OPOER/SYS.TXT";
    // char train[100] = "ARK/BRK/CRK.TXT";
    // char train[100] = "USR/STELL.TXT";
    // char train[100] = "ARK/TWITTER/STELL.TXT";
    // char train[100] = "ARK/BRK/HELLO.TXT";
    writeImg(fat12, train, rootEntry_ptr);

    fclose(fat12);
}

void fillBPB(FILE *fat12, struct BPB *bpb_ptr)
{
    int check;

    //BPB从偏移0个字节处开始
    check = fseek(fat12, 0, SEEK_SET);
    if (check == -1)
        printf("fseek in fillBPB failed!");

    //BPB长度为25字节
    check = fread(bpb_ptr, 1, 36, fat12);
    if (check != 36)
        printf("fread in fillBPB failed!");
}

void printFiles(FILE *fat12, struct RootEntry *rootEntry_ptr)
{
    int base = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec; //根目录首字节的偏移数
    int check;
    char realName[13]; //暂存将空格替换成点后的文件名

    //依次处理根目录中的各个条目
    int i;
    for (i = 0; i < RootEntCnt; i++)
    {

        check = fseek(fat12, base, SEEK_SET);
        if (check == -1)
            printf("fseek in printFiles failed!");

        check = fread(rootEntry_ptr, 1, 32, fat12);
        if (check != 32)
            printf("fread in printFiles failed!");

        base += 32;

        if (rootEntry_ptr->DIR_Name[0] == '\0')
            continue; //空条目不输出

        //过滤非目标文件
        if ((rootEntry_ptr->DIR_Attr & 0x0F) != 0)
            continue; // 过滤掉只读文件，隐藏文件，系统文件，卷标

        int k;
        if ((rootEntry_ptr->DIR_Attr & 0x10) == 0)
        {
            // printf("Dir first cluster = %d\n", rootEntry_ptr->DIR_FstClus);
            // 类型为文件
            int tempLong = -1;
            for (k = 0; k < 11; k++)
            {
                if (rootEntry_ptr->DIR_Name[k] != ' ')
                {
                    tempLong++;
                    realName[tempLong] = rootEntry_ptr->DIR_Name[k];
                }
                if (k == 7)
                { // 添加文件名与后缀名的分隔符
                    tempLong++;
                    realName[tempLong] = '.';
                }
            }
            tempLong++;
            realName[tempLong] = '\0'; //到此为止，把文件名提取出来放到了realName里

            //输出文件
            // printf("%s\n", realName);

            // 存储索引
            rootFileCnt++;
            rootFileList[rootFileCnt] = (struct FileList *)malloc(sizeof(struct FileList)); // 破案了，之前写成struct FileList *了
            rootFileList[rootFileCnt]->DIR_Attr = rootEntry_ptr->DIR_Attr;
            rootFileList[rootFileCnt]->DIR_FileSize = rootEntry_ptr->DIR_FileSize;
            rootFileList[rootFileCnt]->DIR_FstClus = rootEntry_ptr->DIR_FstClus;
            strcpy(rootFileList[rootFileCnt]->full_Name, realName);
            rootFileList[rootFileCnt]->father = NULL;
            // printf("rootFileList[rootFileCnt].full_Name = %s\n", rootFileList[rootFileCnt]->full_Name);
            printf("\e[0;34mFile  \e[0m\e[1;34m%s\e[0m\n", rootFileList[rootFileCnt]->full_Name);
        }
        else
        {
            // 类型为目录
            // printf("Dir Name = %s\n", rootEntry_ptr->DIR_Name);
            // printf("Dir attribute = 0x%02x\n", rootEntry_ptr->DIR_Attr);
            // printf("Dir size = %d\n", rootEntry_ptr->DIR_FileSize);
            // printf("Dir first cluster = %d\n", rootEntry_ptr->DIR_FstClus);
            int tempLong = -1;
            for (k = 0; k < 11; k++)
            {
                if (rootEntry_ptr->DIR_Name[k] != ' ')
                {
                    tempLong++;
                    realName[tempLong] = rootEntry_ptr->DIR_Name[k];
                }
                else
                {
                    tempLong++;
                    realName[tempLong] = '\0';
                    break;
                }
            } //到此为止，把目录名提取出来放到了realName

            // 存储索引
            rootFileCnt++;
            rootFileList[rootFileCnt] = (struct FileList *)malloc(sizeof(struct FileList));
            rootFileList[rootFileCnt]->DIR_Attr = rootEntry_ptr->DIR_Attr;
            rootFileList[rootFileCnt]->DIR_FileSize = rootEntry_ptr->DIR_FileSize;
            rootFileList[rootFileCnt]->DIR_FstClus = rootEntry_ptr->DIR_FstClus;
            strcpy(rootFileList[rootFileCnt]->full_Name, realName);
            rootFileList[rootFileCnt]->father = NULL;
            // printf("rootFileList[rootFileCnt].full_Name = %s\n", rootFileList[rootFileCnt]->full_Name);
            printf("\e[0;31mDir   \e[0m\e[1;31m%s\e[0m\n", rootFileList[rootFileCnt]->full_Name);

            // for (int i = 0; i <= rootFileCnt; i++)
            // {
            //     printf("rootFileName[%d] = %s\n", i, rootFileList[i]->full_Name);
            // }
            // printf("\n");
            //输出目录及子文件
            printChildren(fat12, realName, rootEntry_ptr->DIR_FstClus, rootFileList[rootFileCnt]);
        }
        // for (int i = 0; i <= rootFileCnt; i++)
        // {
        //     printf("rootFileName[%d] = %s\n", i, rootFileList[i]->full_Name);
        // }
    }
}

void printChildren(FILE *fat12, char *directory, int startClus, struct FileList *currentFileList)
{
    // printf("Enter Directory: %s\n", directory);
    //数据区的第一个簇（即2号簇）的偏移字节
    int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
    char fullName[64]; //存放文件路径及全名
    int strLength = strlen(directory);
    strcpy(fullName, directory);
    fullName[strLength] = '/';
    strLength++;
    fullName[strLength] = '\0';
    char *fileName = &fullName[strLength];

    int currentClus = startClus;
    int value = 0;
    int ifOnlyDirectory = 0;
    while (value < 0xFF8)
    {
        value = getFATValue(fat12, currentClus);
        if (value == 0xFF7)
        {
            printf("坏簇，读取失败!\n");
            break;
        }

        char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
        char *content = str;

        int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
        int check;
        check = fseek(fat12, startByte, SEEK_SET);
        if (check == -1)
            printf("fseek in printChildren failed!");

        check = fread(content, 1, SecPerClus * BytsPerSec, fat12);
        if (check != SecPerClus * BytsPerSec)
            printf("fread in printChildren failed!");

        //解析content中的数据,依次处理各个条目,目录下每个条目结构与根目录下的目录结构相同
        int count = SecPerClus * BytsPerSec; //每簇的字节数
        int loop = 0;
        while (loop < count)
        {
            int i;
            char tempName[12]; //暂存替换空格为点后的文件名
            if (content[loop] == '\0')
            {
                loop += 32;
                continue;
            } //空条目不输出

            struct FileEntry *fileEntry;
            fileEntry = (struct FileEntry *)&content[loop];
            // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
            // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
            // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
            // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);
            // printf("Dir loop = %d\n", loop);

            //过滤非目标文件
            int j;
            int boolean = 0;
            for (j = loop; j < loop + 11; j++)
            {
                if (!(((content[j] >= 48) && (content[j] <= 57)) ||
                      ((content[j] >= 65) && (content[j] <= 90)) ||
                      ((content[j] >= 97) && (content[j] <= 122)) ||
                      (content[j] == ' ')))
                {
                    boolean = 1; //非英文及数字、空格
                    break;
                }
            }
            if (boolean == 1)
            {
                loop += 32;
                continue;
            } //非目标文件不输出

            if ((fileEntry->DIR_Attr & 0x0F) != 0)
            {
                loop += 32;
                continue;
            } //非目标文件不输出

            if ((fileEntry->DIR_Attr & 0x10) == 0)
            {
                // 类型为文件
                int k;
                int tempLong = -1;
                for (k = 0; k < 11; k++)
                {
                    if (content[loop + k] != ' ')
                    {
                        tempLong++;
                        tempName[tempLong] = content[loop + k];
                    }
                    if (k == 7)
                    { // 添加文件名与后缀名的分隔符
                        tempLong++;
                        tempName[tempLong] = '.';
                    }
                }
                tempLong++;
                tempName[tempLong] = '\0'; //到此为止，把文件名提取出来放到tempName里
                // printf("Test! |%s|\n", tempName);

                strcpy(fileName, tempName);
                // printf("%s\n", fullName);
                ifOnlyDirectory = 1;
                loop += 32;

                rootFileCnt++;
                rootFileList[rootFileCnt] = (struct FileList *)malloc(sizeof(struct FileList));
                rootFileList[rootFileCnt]->DIR_Attr = fileEntry->DIR_Attr;
                rootFileList[rootFileCnt]->DIR_FileSize = fileEntry->DIR_FileSize;
                rootFileList[rootFileCnt]->DIR_FstClus = fileEntry->DIR_FstClus;
                strcpy(rootFileList[rootFileCnt]->full_Name, fullName);
                rootFileList[rootFileCnt]->father = currentFileList;
                // printf("rootFileList[rootFileCnt].full_Name = %s\n", rootFileList[rootFileCnt]->full_Name);
                printf("\e[0;34mFile  \e[0m\e[1;34m%s\e[0m\n", rootFileList[rootFileCnt]->full_Name);
            }
            else
            {
                // 类型为目录
                int k;
                int tempLong = -1;
                for (k = 0; k < 11; k++)
                {
                    if (content[loop + k] != ' ')
                    {
                        tempLong++;
                        tempName[tempLong] = content[loop + k];
                    }
                    else
                    {
                        tempLong++;
                        tempName[tempLong] = '\0';
                        break;
                    }
                }
                tempLong++;
                tempName[tempLong] = '\0'; //到此为止，把文件名提取出来放到tempName里
                // printf("Test! |%s|\n", tempName);

                strcpy(fileName, tempName);
                // printf("%s\n", fullName);
                ifOnlyDirectory = 1;
                loop += 32;

                rootFileCnt++;
                rootFileList[rootFileCnt] = (struct FileList *)malloc(sizeof(struct FileList));
                rootFileList[rootFileCnt]->DIR_Attr = fileEntry->DIR_Attr;
                rootFileList[rootFileCnt]->DIR_FileSize = fileEntry->DIR_FileSize;
                rootFileList[rootFileCnt]->DIR_FstClus = fileEntry->DIR_FstClus;
                strcpy(rootFileList[rootFileCnt]->full_Name, fullName);
                rootFileList[rootFileCnt]->father = currentFileList;
                // printf("rootFileList[rootFileCnt].full_Name = %s\n", rootFileList[rootFileCnt]->full_Name);
                printf("\e[0;31mDir   \e[0m\e[1;31m%s\e[0m\n", rootFileList[rootFileCnt]->full_Name);

                // 继续输出目录以及子文件
                printChildren(fat12, fullName, fileEntry->DIR_FstClus, rootFileList[rootFileCnt]);
            }
        }
        free(str);

        currentClus = value;
    };

    // if (ifOnlyDirectory == 0)
    //     printf("%s\n", fullName); //空目录的情况下，输出目录
}

int getFATValue(FILE *fat12, int num)
{
    //FAT1的偏移字节
    int fatBase = RsvdSecCnt * BytsPerSec;
    //FAT项的偏移字节
    int fatPos = fatBase + num * 3 / 2;
    //奇偶FAT项处理方式不同，分类进行处理，从0号FAT项开始
    int type = 0;
    if (num % 2 == 0)
    {
        type = 0;
    }
    else
    {
        type = 1;
    }

    //先读出FAT项所在的两个字节
    u16 bytes;
    u16 *bytes_ptr = &bytes;
    int check;
    check = fseek(fat12, fatPos, SEEK_SET);
    if (check == -1)
        printf("fseek in getFATValue failed!");

    check = fread(bytes_ptr, 1, 2, fat12);
    if (check != 2)
        printf("fread in getFATValue failed!");

    //u16为short，结合存储的小尾顺序和FAT项结构可以得到
    //type为0的话，取byte2的低4位和byte1构成的值，type为1的话，取byte2和byte1的高4位构成的值
    if (type == 0)
    {
        bytes = bytes << 4;
        return bytes >> 4;
    }
    else
    {
        return bytes >> 4;
    }
}

int matchEntry(char *directory)
{
    int i;
    for (i = 0; i <= rootFileCnt; i++)
    {
        // printf("rootFileName[%d] = %s\n", i, rootFileList[i]->full_Name);
        if (strcmp(directory, rootFileList[i]->full_Name) == 0)
        {
            printf("rootFileName[%d] = %s\n", i, rootFileList[i]->full_Name);
            return i;
        }
    }
    return -1;
}

int readImg(FILE *fat12, char *directory)
{
    // int entry = matchEntry(directory);
    int entry = matchEntry(fatPATH);
    if (entry == -1)
    {
        printf("File not found.\n");
        return -1;
    }
    else
    {
        printf("File entry is %d\n", entry);
    }

    //数据区的第一个簇（即2号簇）的偏移字节
    int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
    int currentClus = rootFileList[entry]->DIR_FstClus;
    int value = 0;
    int fileSize = rootFileList[entry]->DIR_FileSize;

    // printf("The FAT chain of file %s:\n", rootFileList[entry]->full_Name);
    char *outBuffer;
    outBuffer = (char *)malloc(sizeof(char) * 8192);
    char *outBufferCurrent;
    outBufferCurrent = outBuffer;
    while (value < 0xFF8)
    {
        // printf("Reading Cluster %d\n", currentClus);
        value = getFATValue(fat12, currentClus);
        if (value == 0xFF7)
        {
            printf("坏簇，读取失败!\n");
            break;
        }

        char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
        char *content = str;

        int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
        int check;
        check = fseek(fat12, startByte, SEEK_SET);
        if (check == -1)
            printf("fseek in printChildren failed!");

        check = fread(content, 1, SecPerClus * BytsPerSec, fat12);
        if (check != SecPerClus * BytsPerSec)
            printf("fread in printChildren failed!");

        memcpy(outBufferCurrent, content, BytsPerSec);
        outBufferCurrent += BytsPerSec;

        free(str);
        currentClus = value;
    };
    // printf("File Content = %s\n", outBuffer);

    FILE *outputFile;
    outputFile = fopen(sysPATH, "wt+");
    // outputFile = fopen("hello3.txt", "wt+"); //打开FAT12的映像文件
    // fprintf(outputFile, "%s", outBuffer);
    fwrite(outBuffer, sizeof(char), fileSize, outputFile);
    // 刷新缓冲区，将缓冲区的内容写入文件
    fflush(outputFile);
    // 重置文件内部位置指针，让位置指针指向文件开头
    rewind(outputFile);
    fclose(outputFile);
    return fileSize;
}

int checkFree(FILE *fat12)
{
    int currentClus, value;

    for (int i = 0; i < 2880; i++)
    {
        freeSector[i] = -1;
    }
    freeSector[0] = INT16_MAX;
    freeSector[1] = INT16_MAX;

    for (int i = 0; i <= rootFileCnt; i++)
    {
        currentClus = rootFileList[i]->DIR_FstClus;
        value = 0;
        // printf("The FAT chain of file %s:\n", rootFileList[i]->full_Name);
        while (value < 0xFF8)
        {
            value = getFATValue(fat12, currentClus);
            if (value == 0xFF7)
            {
                printf("坏簇，读取失败!\n");
                break;
            }
            // printf("clus = %4x, value = %4x\n", currentClus, value);
            freeSector[currentClus] = i;
            currentClus = value;
        };
    }
    // for (int i = 0; i < 102; i++)
    // {
    //     printf("Sect %4d, cond %2d\n", i, freeSector[i]);
    // }
    int freeSectorNum = 0;
    for (int i = 0; i < 2880; i++)
    {
        if (freeSector[i] == -1)
        {
            freeSectorNum++;
        }
    }
    return freeSectorNum;
}

int existsEntry(char *directory)
{
    for (int i = 0; i <= rootFileCnt; i++)
    {
        if (strcmp(directory, rootFileList[i]->full_Name) == 0)
        {
            return i;
        }
    }
    return -1;
}

int writeImg(FILE *fat12, char *directory, struct RootEntry *rootEntry_ptr)
{
    // 检查是否已经存在文件
    if (existsEntry(directory) >= 0)
    {
        printf("\e[0;32mDirectory = \e[0m\e[1;32m%s\e[0m\n", directory);
        printf("\e[0;32mFile already exists.\e[0m\n");
        return -1;
    }
    // 在根目录插入
    int entry = -1;
    if (strchr(directory, '/') == NULL)
    {
        entry = createRootEntry(fat12, directory, ENTRY_FILE);
    }
    else
    {
        char delim[2] = "/";
        char *tempPath;
        tempPath = (char *)malloc(sizeof(char) * strlen(directory));
        strcpy(tempPath, directory);
        char *beginPath;
        beginPath = strtok(tempPath, delim);
        // printf("beginpath = %s\n", beginPath);
        entry = existsEntry(beginPath);
        if (entry >= 0)
        {
            // 根目录存在，继续进行下一级插入
            printf("\e[0;32mExists root directory \e[0m\e[1;32m%s\e[0m\n", beginPath);
            char *endPath;
            endPath = (char *)malloc(sizeof(char) * (strlen(directory) - strlen(beginPath)));
            endPath = directory + strlen(beginPath) + 1;
            beginPath[strlen(beginPath) + 1] = '\0';
            beginPath[strlen(beginPath)] = '/';
            // printf("beginpath = %s, endPath = %s\n", beginPath, endPath);
            createSubEntry(fat12, beginPath, endPath, directory, entry);
        }
        else
        {
            // 根目录下不存在该目录，先创建根目录下的目录
            entry = createRootEntry(fat12, beginPath, ENTRY_DIR);
            // 然后进行下一级插入
            char *endPath;
            endPath = (char *)malloc(sizeof(char) * (strlen(directory) - strlen(beginPath)));
            endPath = directory + strlen(beginPath) + 1;
            beginPath[strlen(beginPath) + 1] = '\0';
            beginPath[strlen(beginPath)] = '/';
            // printf("beginpath = %s, endPath = %s\n", beginPath, endPath);
            createSubEntry(fat12, beginPath, endPath, directory, entry);
        }
    }
    return 0;
}

int seekEmptyCluster()
{
    for (int i = 0; i < 2880; i++)
    {
        if (freeSector[i] < 0)
        {
            return i;
        }
    }
    return -1;
}

int writeFATValue(FILE *fat12, int num, int next)
{
    //FAT1的偏移字节
    int fatBase = RsvdSecCnt * BytsPerSec;
    //FAT项的偏移字节
    int fatPos = fatBase + num * 3 / 2;
    //奇偶FAT项处理方式不同，分类进行处理，从0号FAT项开始
    int type = 0;
    if (num % 2 == 0)
    {
        type = 0;
    }
    else
    {
        type = 1;
    }

    //先读出FAT项所在的两个字节
    u16 bytes;
    u16 *bytes_ptr = &bytes;
    int check;
    check = fseek(fat12, fatPos, SEEK_SET);
    if (check == -1)
        printf("fseek in writeFATValue failed!");

    check = fread(bytes_ptr, 1, 2, fat12);
    if (check != 2)
        printf("fread in writeFATValue failed!");

    if (type == 0)
    {
        check = fseek(fat12, fatPos, SEEK_SET);
        *bytes_ptr = next | ((*bytes_ptr) & 0xf000);
        check = fwrite(bytes_ptr, 1, 2, fat12);
        check = fseek(fat12, fatPos, SEEK_SET);
        check = fread(bytes_ptr, 1, 2, fat12);
        // printf("FAT1: type 0, num = %d, next = %d, fat = %4x\n", num, next, *bytes_ptr);
    }
    else
    {
        check = fseek(fat12, fatPos, SEEK_SET);
        *bytes_ptr = (next << 4) | ((*bytes_ptr) & 0x000f);
        check = fwrite(bytes_ptr, 1, 2, fat12);
        check = fseek(fat12, fatPos, SEEK_SET);
        check = fread(bytes_ptr, 1, 2, fat12);
        // printf("FAT1: type 1, num = %d, next = %d, fat = %4x\n", num, next, *bytes_ptr);
    }

    //u16为short，结合存储的小尾顺序和FAT项结构可以得到
    //type为0的话，取byte2的低4位和byte1构成的值，type为1的话，取byte2和byte1的高4位构成的值
    // if (type == 0)
    // {
    //     bytes = bytes << 4;
    //     return bytes >> 4;
    // }
    // else
    // {
    //     return bytes >> 4;
    // }
    fatBase = fatBase + FATSz * BytsPerSec;
    fatPos = fatBase + num * 3 / 2;
    check = fseek(fat12, fatPos, SEEK_SET);
    check = fwrite(bytes_ptr, 1, 2, fat12);
    check = fseek(fat12, fatPos, SEEK_SET);
    check = fread(bytes_ptr, 1, 2, fat12);
    // printf("FAT2: num = %d, next = %d, fat = %4x\n", num, next, *bytes_ptr);
    return next;
}

int createRootEntry(FILE *fat12, char *directory, int type)
{
    if (type == ENTRY_DIR)
    {
        printf("\e[0;35mCreate root directory \e[0m\e[1;35m%s\e[0m\n", directory);
    }
    else if (type == ENTRY_FILE)
    {
        printf("\e[0;35mCreate root file \e[0m\e[1;35m%s\e[0m\n", directory);
    }
    rootFileCnt++;
    rootFileList[rootFileCnt] = (struct FileList *)malloc(sizeof(struct FileList)); // 破案了，之前写成struct FileList *了
    strcpy(rootFileList[rootFileCnt]->full_Name, directory);
    if (type == ENTRY_DIR)
    {
        // 创建目录
        int newCluster = seekEmptyCluster();
        if (newCluster >= 0)
        {
            freeSector[newCluster] = rootFileCnt;
        }
        struct RootEntry *rootEntry;
        rootEntry = (struct RootEntry *)malloc(sizeof(struct RootEntry));
        int base = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec; //根目录首字节的偏移数

        int i, check;
        for (i = 0; i < RootEntCnt; i++)
        {
            // printf("i = %d\n", i);
            check = fseek(fat12, base, SEEK_SET);
            if (check == -1)
                printf("fseek in createRootEntry failed!");

            check = fread(rootEntry, 1, 32, fat12);
            if (check != 32)
                printf("fread in createRootEntry failed!");

            base += 32;

            //过滤非目标文件
            if ((rootEntry->DIR_Attr & 0x0F) != 0)
                continue; // 过滤掉只读文件，隐藏文件，系统文件，卷标

            if (rootEntry->DIR_Name[0] == '\0')
                break; // 空条目，可以开始
        }
        if (i >= RootEntCnt)
        {
            printf("根目录空间不足，不能创建\n");
            return -1;
        }
        int directoryLen = strlen(directory);
        for (int k = 0; k < 11; k++) // 重要，之前写成8，导致后面的空格没了
        {
            if (k >= directoryLen)
            {
                rootEntry->DIR_Name[k] = ' ';
            }
            else
            {
                rootEntry->DIR_Name[k] = directory[k];
            }
        }
        // printf("RootEntry = %d, DIR_Name = %s\n", i, rootEntry->DIR_Name);
        rootEntry->DIR_FileSize = 0;
        rootEntry->DIR_FstClus = newCluster;
        rootEntry->DIR_Attr = 0x10; // 目录
        rootEntry->DIR_WrtDate = 0x52c5;
        rootEntry->DIR_WrtTime = 0x01fa;

        rootFileList[rootFileCnt]->DIR_Attr = 0x10;
        rootFileList[rootFileCnt]->DIR_FileSize = 0;
        rootFileList[rootFileCnt]->DIR_FstClus = newCluster;
        rootFileList[rootFileCnt]->father = NULL;

        base -= 32;
        check = fseek(fat12, base, SEEK_SET);
        if (check == -1)
            printf("fseek in createRootEntry failed!");

        check = fwrite(rootEntry, 1, 32, fat12);
        if (check != 32)
            printf("fwrites in createRootEntry failed!");

        writeFATValue(fat12, newCluster, 4095);

        // 写入.和..
        int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
        int value = 0;
        int currentClus = newCluster;
        while (value < 0xFF8)
        {
            value = getFATValue(fat12, currentClus);
            if (value == 0xFF7)
            {
                printf("坏簇，读取失败!\n");
                break;
            }

            char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
            // char *content = str;
            char *content;
            content = (char *)malloc(SecPerClus * BytsPerSec);
            memset(content, 0, SecPerClus * BytsPerSec);

            int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
            int check;
            check = fseek(fat12, startByte, SEEK_SET);
            if (check == -1)
                printf("fseek in createRootEntry failed!");

            //解析content中的数据,依次处理各个条目,目录下每个条目结构与根目录下的目录结构相同
            int count = SecPerClus * BytsPerSec; //每簇的字节数
            int loop = 0;
            struct FileEntry *fileEntry;
            fileEntry = (struct FileEntry *)&content[loop];
            memcpy(fileEntry->DIR_Name, ".          ", 11); // 加了空格！原来没有
            fileEntry->DIR_Attr = 0x10;
            fileEntry->DIR_FileSize = 0;
            fileEntry->DIR_FstClus = newCluster;
            fileEntry->DIR_WrtDate = 0x52c5;
            fileEntry->DIR_WrtTime = 0x01fa;
            // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
            // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
            // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
            // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);

            loop = 32;
            fileEntry = (struct FileEntry *)&content[loop];
            memcpy(fileEntry->DIR_Name, "..         ", 11);
            fileEntry->DIR_Attr = 0x10;
            fileEntry->DIR_FileSize = 0;
            fileEntry->DIR_FstClus = 0;
            fileEntry->DIR_WrtDate = 0x52c5;
            fileEntry->DIR_WrtTime = 0x01fa;
            // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
            // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
            // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
            // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);

            check = fseek(fat12, startByte, SEEK_SET);
            if (check == -1)
                printf("fseek in createRootEntry failed!");

            check = fwrite(content, 1, SecPerClus * BytsPerSec, fat12);
            if (check != SecPerClus * BytsPerSec)
                printf("fwrite in createRootEntry failed!");

            free(str);

            currentClus = value;
        };
    }
    else if (type == ENTRY_FILE)
    {
        // 创建文件
        int newCluster = seekEmptyCluster();
        if (newCluster >= 0)
        {
            freeSector[newCluster] = rootFileCnt;
        }
        struct RootEntry *rootEntry;
        rootEntry = (struct RootEntry *)malloc(sizeof(struct RootEntry));
        int base = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec; //根目录首字节的偏移数

        int i, check;
        for (i = 0; i < RootEntCnt; i++)
        {
            // printf("i = %d\n", i);
            check = fseek(fat12, base, SEEK_SET);
            if (check == -1)
                printf("fseek in createRootEntry failed!");

            check = fread(rootEntry, 1, 32, fat12);
            if (check != 32)
                printf("fread in createRootEntry failed!");

            base += 32;

            //过滤非目标文件
            if ((rootEntry->DIR_Attr & 0x0F) != 0)
                continue; // 过滤掉只读文件，隐藏文件，系统文件，卷标

            if (rootEntry->DIR_Name[0] == '\0')
                break; // 空条目，可以开始
        }
        if (i >= RootEntCnt)
        {
            printf("根目录空间不足，不能创建\n");
            return -1;
        }
        int directoryLen = strlen(directory);
        int delimPoint = 0;
        for (int k = 0; k < 8; k++)
        {
            if (directory[k] == '.')
            {
                delimPoint = k;
                while (k < 8)
                {
                    rootEntry->DIR_Name[k] = ' ';
                    k++;
                }
                break;
            }
            else
            {
                rootEntry->DIR_Name[k] = directory[k];
            }
        }
        for (int k = 8; k < 11; k++)
        {
            if (delimPoint >= strlen(directory))
            {
                rootEntry->DIR_Name[k] = ' ';
            }
            else
            {
                delimPoint++;
                rootEntry->DIR_Name[k] = directory[delimPoint];
            }
        }
        // printf("single!\n");
        // for (int k = 0; k < 11; k++)
        // {
        //     printf("%c", rootEntry->DIR_Name[k]);
        // }
        // printf("end!\n");
        // printf("RootEntry = %d, DIR_Name = %s\n", i, rootEntry->DIR_Name);
        // 读取写入文件信息
        FILE *inputFile;
        inputFile = fopen(sysPATH, "rb");
        fseek(inputFile, 0L, SEEK_END);
        int fileSize = ftell(inputFile);
        // printf("size = %d\n", fileSize);

        rootEntry->DIR_FileSize = fileSize;
        rootEntry->DIR_FstClus = newCluster;
        rootEntry->DIR_Attr = 0x0; // 文件
        rootEntry->DIR_WrtDate = 0x52c5;
        rootEntry->DIR_WrtTime = 0x01fa;

        rootFileList[rootFileCnt]->DIR_Attr = 0x0;
        rootFileList[rootFileCnt]->DIR_FileSize = fileSize;
        rootFileList[rootFileCnt]->DIR_FstClus = newCluster;
        rootFileList[rootFileCnt]->father = NULL;

        base -= 32;
        check = fseek(fat12, base, SEEK_SET);
        if (check == -1)
            printf("fseek in createRootEntry failed!");

        check = fwrite(rootEntry, 1, 32, fat12);
        if (check != 32)
            printf("fwrites in createRootEntry failed!");

        int clusterCount = (fileSize - 1) / BytsPerSec + 1;
        for (int i = 1; i < clusterCount; i++)
        {
            int nextCluster = seekEmptyCluster();
            if (nextCluster >= 0)
            {
                freeSector[nextCluster] = rootFileCnt;
            }
            writeFATValue(fat12, newCluster, nextCluster);
            newCluster = nextCluster;
        }
        writeFATValue(fat12, newCluster, 4095);

        // 写入文件内容
        int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
        int value = 0;
        int currentClus = rootFileList[rootFileCnt]->DIR_FstClus;
        fseek(inputFile, 0, SEEK_SET);
        while (value < 0xFF8)
        {
            value = getFATValue(fat12, currentClus);
            if (value == 0xFF7)
            {
                printf("坏簇，读取失败!\n");
                break;
            }

            char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
            // char *content = str;
            char *content;
            content = (char *)malloc(SecPerClus * BytsPerSec);
            memset(content, 0, SecPerClus * BytsPerSec);

            int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
            int check;
            check = fseek(fat12, startByte, SEEK_SET);
            if (check == -1)
                printf("fseek in createRootEntry failed!");

            fread(content, 1, SecPerClus * BytsPerSec, inputFile);
            // strcpy(content, "This is a test file.");

            check = fseek(fat12, startByte, SEEK_SET);
            if (check == -1)
                printf("fseek in createRootEntry failed!");

            check = fwrite(content, 1, SecPerClus * BytsPerSec, fat12);
            if (check != SecPerClus * BytsPerSec)
                printf("fwrite in createRootEntry failed!");

            free(str);
            free(content);

            currentClus = value;
        };
        fclose(inputFile);
    }
    return rootFileCnt; // 原本为0！！，不对，应该返回正确的
}

int createSubEntry(FILE *fat12, char *processedPath, char *remainPath, char *directory, int preEntry)
{
    // printf("create sub entry\n");
    if (strchr(remainPath, '/') == NULL)
    {
        // 已经只剩下文件了
        printf("\e[0;35mCreate sub file \e[0m\e[1;35m%s\e[0m\n", directory);
        // 进行插入（因为有preEntry的存在）

        // 创建文件
        rootFileCnt++;
        rootFileList[rootFileCnt] = (struct FileList *)malloc(sizeof(struct FileList)); // 破案了，之前写成struct FileList *了
        strcpy(rootFileList[rootFileCnt]->full_Name, directory);

        int newCluster = seekEmptyCluster();
        if (newCluster >= 0)
        {
            freeSector[newCluster] = rootFileCnt;
        }

        int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
        int currentClus = rootFileList[preEntry]->DIR_FstClus;
        int value = 0;
        int ifOnlyDirectory = 0;
        while (value < 0xFF8)
        {
            value = getFATValue(fat12, currentClus);
            if (value == 0xFF7)
            {
                printf("坏簇，读取失败!\n");
                break;
            }

            char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
            char *content = str;

            int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
            int check;
            check = fseek(fat12, startByte, SEEK_SET);
            if (check == -1)
                printf("fseek in createSubEntry failed!");

            check = fread(content, 1, SecPerClus * BytsPerSec, fat12);
            if (check != SecPerClus * BytsPerSec)
                printf("fread in createSubEntry failed!");

            //解析content中的数据,依次处理各个条目,目录下每个条目结构与根目录下的目录结构相同
            int count = SecPerClus * BytsPerSec; //每簇的字节数
            int loop = 0;
            struct FileEntry *fileEntry;
            while (loop < count)
            {
                int i;
                char tempName[12]; //暂存替换空格为点后的文件名
                if (content[loop] != '\0')
                {
                    fileEntry = (struct FileEntry *)&content[loop];
                    // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
                    // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
                    // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
                    // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);
                    // printf("Dir loop = %d\n", loop);
                    loop += 32;
                    continue;
                } // 有存储的条目不输出

                // struct FileEntry *fileEntry; // 新添加的（即将被写入的文件）
                // fileEntry = (struct FileEntry *)malloc(sizeof(struct FileEntry));

                // struct FileEntry *fileEntry;
                fileEntry = (struct FileEntry *)&content[loop];
                // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
                // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
                // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
                // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);
                // printf("Dir loop = %d\n", loop);

                break;
            }
            // 读取写入文件信息
            FILE *inputFile;
            inputFile = fopen(sysPATH, "rb");
            fseek(inputFile, 0L, SEEK_END);
            int fileSize = ftell(inputFile);
            // printf("size = %d\n", fileSize);

            fileEntry->DIR_FileSize = fileSize;
            fileEntry->DIR_FstClus = newCluster;
            fileEntry->DIR_Attr = 0x0; // 文件
            fileEntry->DIR_WrtDate = 0x52c5;
            fileEntry->DIR_WrtTime = 0x01fa;

            rootFileList[rootFileCnt]->DIR_Attr = 0x0;
            rootFileList[rootFileCnt]->DIR_FileSize = fileSize;
            rootFileList[rootFileCnt]->DIR_FstClus = newCluster;
            rootFileList[rootFileCnt]->father = rootFileList[preEntry];

            // strcpy(fileEntry->DIR_Name, "");
            // printf("FILE NAME = %s\n", remainPath);
            int remainLen = strlen(remainPath);
            int delimPoint = 0;
            for (int k = 0; k < 8; k++)
            {
                if (remainPath[k] == '.')
                {
                    delimPoint = k;
                    while (k < 8)
                    {
                        fileEntry->DIR_Name[k] = ' ';
                        k++;
                    }
                    break;
                }
                else
                {
                    fileEntry->DIR_Name[k] = remainPath[k];
                }
            }
            for (int k = 8; k < 11; k++)
            {
                if (delimPoint >= strlen(remainPath))
                {
                    fileEntry->DIR_Name[k] = ' ';
                }
                else
                {
                    delimPoint++;
                    fileEntry->DIR_Name[k] = remainPath[delimPoint];
                }
            }
            // printf("single!\n");
            // for (int k = 0; k < 11; k++)
            // {
            //     printf("%c", fileEntry->DIR_Name[k]);
            // }
            // printf("end!\n");
            // printf("DIR_Name = %s\n", fileEntry->DIR_Name);

            check = fseek(fat12, startByte, SEEK_SET);
            if (check == -1)
                printf("fseek in createSubEntry failed!");

            check = fwrite(content, 1, SecPerClus * BytsPerSec, fat12);
            if (check != SecPerClus * BytsPerSec)
                printf("fwrite in createSubEntry failed!");

            int clusterCount = (fileSize - 1) / BytsPerSec + 1;
            for (int i = 1; i < clusterCount; i++)
            {
                int nextCluster = seekEmptyCluster();
                if (nextCluster >= 0)
                {
                    freeSector[nextCluster] = rootFileCnt;
                }
                writeFATValue(fat12, newCluster, nextCluster);
                newCluster = nextCluster;
            }
            writeFATValue(fat12, newCluster, 4095);
            free(str);

            // 写入真正的文件内容
            int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
            int value = 0;
            currentClus = rootFileList[rootFileCnt]->DIR_FstClus;
            fseek(inputFile, 0, SEEK_SET);
            while (value < 0xFF8)
            {
                value = getFATValue(fat12, currentClus);
                if (value == 0xFF7)
                {
                    printf("坏簇，读取失败!\n");
                    break;
                }

                char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
                // char *content = str;
                char *content;
                content = (char *)malloc(SecPerClus * BytsPerSec);
                memset(content, 0, SecPerClus * BytsPerSec);

                int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
                int check;
                check = fseek(fat12, startByte, SEEK_SET);
                if (check == -1)
                    printf("fseek in createRootEntry failed!");

                fread(content, 1, SecPerClus * BytsPerSec, inputFile);
                // strcpy(content, "This is a test file in directory.");

                check = fseek(fat12, startByte, SEEK_SET);
                if (check == -1)
                    printf("fseek in createRootEntry failed!");

                check = fwrite(content, 1, SecPerClus * BytsPerSec, fat12);
                if (check != SecPerClus * BytsPerSec)
                    printf("fwrite in createRootEntry failed!");

                free(str);
                free(content);

                currentClus = value;
            };

            // currentClus = value;
        };

        // printf("只剩下文件了，beginPath = %s, remainPath = %s\n", processedPath, remainPath);
    }
    else
    {
        // 还有目录，开始比较
        char delim[2] = "/";
        char *tempPath;
        tempPath = (char *)malloc(sizeof(char) * strlen(remainPath));
        strcpy(tempPath, remainPath);
        char *beginPath;
        beginPath = strtok(tempPath, delim);
        // printf("beginpath = %s\n", beginPath);
        char *realBeginPath;
        realBeginPath = (char *)malloc(sizeof(char) * strlen(directory));
        strcat(realBeginPath, processedPath);
        strcat(realBeginPath, beginPath);
        // printf("realbeginpath = %s\n", realBeginPath);
        int entry = -1;
        entry = existsEntry(realBeginPath);
        if (entry < 0)
        {
            // 不存在目录，先创建目录
            printf("\e[0;35mCreate sub directory \e[0m\e[1;35m%s\e[0m\n", realBeginPath);
            // 进行preEntry下的目录创建(获取到现在的entry)
            // 创建目录
            rootFileCnt++;
            rootFileList[rootFileCnt] = (struct FileList *)malloc(sizeof(struct FileList)); // 破案了，之前写成struct FileList *了
            strcpy(rootFileList[rootFileCnt]->full_Name, directory);
            rootFileList[rootFileCnt]->DIR_Attr = 0x10;
            rootFileList[rootFileCnt]->DIR_FileSize = 0;

            int newCluster = seekEmptyCluster();
            if (newCluster >= 0)
            {
                freeSector[newCluster] = rootFileCnt;
            }

            rootFileList[rootFileCnt]->DIR_FstClus = newCluster;
            strcpy(rootFileList[rootFileCnt]->full_Name, directory);

            int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
            int currentClus = rootFileList[preEntry]->DIR_FstClus;
            int value = 0;
            int ifOnlyDirectory = 0;
            while (value < 0xFF8)
            {
                value = getFATValue(fat12, currentClus);
                if (value == 0xFF7)
                {
                    printf("坏簇，读取失败!\n");
                    break;
                }

                char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
                char *content = str;

                int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
                int check;
                check = fseek(fat12, startByte, SEEK_SET);
                if (check == -1)
                    printf("fseek in createSubEntry failed!");

                check = fread(content, 1, SecPerClus * BytsPerSec, fat12);
                if (check != SecPerClus * BytsPerSec)
                    printf("fread in createSubEntry failed!");

                //解析content中的数据,依次处理各个条目,目录下每个条目结构与根目录下的目录结构相同
                int count = SecPerClus * BytsPerSec; //每簇的字节数
                int loop = 0;
                struct FileEntry *fileEntry;
                while (loop < count)
                {
                    int i;
                    char tempName[12]; //暂存替换空格为点后的文件名
                    if (content[loop] != '\0')
                    {
                        fileEntry = (struct FileEntry *)&content[loop];
                        // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
                        // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
                        // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
                        // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);
                        // printf("Dir loop = %d\n", loop);
                        loop += 32;
                        continue;
                    } // 有存储的条目不输出

                    // struct FileEntry *fileEntry; // 新添加的（即将被写入的文件）
                    fileEntry = (struct FileEntry *)&content[loop];
                    // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
                    // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
                    // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
                    // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);
                    // printf("Dir loop = %d\n", loop);

                    break;
                }
                if (loop >= count)
                { // 需要到第二条目的文件表，一个扇区装不下
                    // 注：这里没考虑到为目录新开一个扇区的情况
                    currentClus = value;
                    continue;
                }
                fileEntry->DIR_FileSize = 0;
                fileEntry->DIR_FstClus = newCluster;
                fileEntry->DIR_Attr = 0x10; // 目录
                fileEntry->DIR_WrtDate = 0x52c5;
                fileEntry->DIR_WrtTime = 0x01fa;

                rootFileList[rootFileCnt]->DIR_Attr = 0x10;
                rootFileList[rootFileCnt]->DIR_FileSize = 0;
                rootFileList[rootFileCnt]->DIR_FstClus = newCluster;
                rootFileList[rootFileCnt]->father = rootFileList[preEntry];

                // strcpy(fileEntry->DIR_Name, "");
                // printf("FILE NAME = %s\n", remainPath);
                int remainLen = strlen(beginPath);
                int delimPoint = 0;
                for (int k = 0; k < 11; k++)
                {
                    if (k < remainLen)
                    {
                        fileEntry->DIR_Name[k] = beginPath[k];
                    }
                    else
                    {
                        fileEntry->DIR_Name[k] = ' ';
                    }
                }
                // printf("single!\n");
                // for (int k = 0; k < 11; k++)
                // {
                //     printf("%c", fileEntry->DIR_Name[k]);
                // }
                // printf("end!\n");
                // printf("DIR_Name = %s\n", fileEntry->DIR_Name);

                check = fseek(fat12, startByte, SEEK_SET);
                if (check == -1)
                    printf("fseek in createSubEntry failed!");

                check = fwrite(content, 1, SecPerClus * BytsPerSec, fat12);
                if (check != SecPerClus * BytsPerSec)
                    printf("fwrite in createSubEntry failed!");

                writeFATValue(fat12, newCluster, 4095);
                free(str);

                // 写入目录内容
                int dataBase = BytsPerSec * (RsvdSecCnt + FATSz * NumFATs + (RootEntCnt * 32 + BytsPerSec - 1) / BytsPerSec);
                int value = 0;
                currentClus = newCluster;
                while (value < 0xFF8)
                {
                    value = getFATValue(fat12, currentClus);
                    if (value == 0xFF7)
                    {
                        printf("坏簇，读取失败!\n");
                        break;
                    }

                    char *str = (char *)malloc(SecPerClus * BytsPerSec); //暂存从簇中读出的数据
                    // char *content = str;
                    char *content;
                    content = (char *)malloc(SecPerClus * BytsPerSec);
                    memset(content, 0, SecPerClus * BytsPerSec);

                    int startByte = dataBase + (currentClus - 2) * SecPerClus * BytsPerSec;
                    int check;
                    check = fseek(fat12, startByte, SEEK_SET);
                    if (check == -1)
                        printf("fseek in createRootEntry failed!");

                    // strcpy(content, "This is a test file in directory.");
                    int count = SecPerClus * BytsPerSec; //每簇的字节数
                    int loop = 0;
                    struct FileEntry *fileEntry;
                    fileEntry = (struct FileEntry *)&content[loop];
                    memcpy(fileEntry->DIR_Name, ".          ", 11);
                    fileEntry->DIR_Attr = 0x10;
                    fileEntry->DIR_FileSize = 0;
                    fileEntry->DIR_FstClus = newCluster;
                    // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
                    // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
                    // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
                    // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);

                    loop = 32;
                    fileEntry = (struct FileEntry *)&content[loop];
                    memcpy(fileEntry->DIR_Name, "..         ", 11);
                    fileEntry->DIR_Attr = 0x10;
                    fileEntry->DIR_FileSize = 0;
                    fileEntry->DIR_FstClus = rootFileList[preEntry]->DIR_FstClus; // 之前写成0了
                    // printf("\n\nDir Name = %s\n", fileEntry->DIR_Name);
                    // printf("Dir attribute = 0x%02x\n", fileEntry->DIR_Attr);
                    // printf("Dir size = %d\n", fileEntry->DIR_FileSize);
                    // printf("Dir first cluster = %d\n", fileEntry->DIR_FstClus);

                    check = fseek(fat12, startByte, SEEK_SET);
                    if (check == -1)
                        printf("fseek in createRootEntry failed!");

                    check = fwrite(content, 1, SecPerClus * BytsPerSec, fat12);
                    if (check != SecPerClus * BytsPerSec)
                        printf("fwrite in createRootEntry failed!");
                    free(str);

                    currentClus = value;
                };
            };

            // 然后进行下一级插入
            char *endPath;
            endPath = (char *)malloc(sizeof(char) * (strlen(directory) - strlen(realBeginPath)));
            endPath = directory + strlen(realBeginPath) + 1;
            realBeginPath[strlen(realBeginPath) + 1] = '\0';
            realBeginPath[strlen(realBeginPath)] = '/';
            // printf("先创目录，然后beginpath = %s, endPath = %s\n", realBeginPath, endPath);
            entry = rootFileCnt;
            createSubEntry(fat12, realBeginPath, endPath, directory, entry);
        }
        else
        {
            // 目录存在，直接进行下一级插入
            printf("\e[0;32mExists sub directory \e[0m\e[1;32m%s\e[0m\n", realBeginPath);
            char *endPath;
            endPath = (char *)malloc(sizeof(char) * (strlen(directory) - strlen(realBeginPath)));
            endPath = directory + strlen(realBeginPath) + 1;
            realBeginPath[strlen(realBeginPath) + 1] = '\0';
            realBeginPath[strlen(realBeginPath)] = '/';
            // printf("存在目录，然后beginpath = %s, endPath = %s\n", realBeginPath, endPath);
            createSubEntry(fat12, realBeginPath, endPath, directory, entry);
        }
    }

    return 0;
}