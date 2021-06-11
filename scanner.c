#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PATH_LEN (256)
#define HASHSIZE 128
#define NULLKEY 0

typedef struct
{
    unsigned long long value;
    int count;
    char *dir[128];
} HashValue;

HashValue H[128];

typedef struct
{
    unsigned long long value;
    char *dir;
} DirValue;

int InitHashTable()
{
    for (int i = 0; i < HASHSIZE; i++)
    {
        H[i].value = 0;
        H[i].count = 0;
        for (int j = 0; j < 128; j++)
        {
            H[i].dir[j] = NULL;
        }
    }
    return 0;
}

int Hash(long long key)
{
    return key % HASHSIZE;
}

void InsertHashTable(DirValue data)
{
    // printf("addr = %d\n", data.value);
    int addr = Hash(data.value);
    // printf("first addr = %d\n", addr);
    while ((H[addr].count != 0) && (H[addr].value != data.value))
    {
        addr = (addr + 1) % HASHSIZE;
    }
    // printf("final addr = %d\n", addr);s
    H[addr].value = data.value;
    H[addr].dir[H[addr].count] = data.dir;
    H[addr].count++;
    // printf("dir = %s, count = %d\n", data.dir, H[addr].count);

    return;
}

int SearchHashTable(DirValue data)
{
    unsigned long long key = data.value;
    int addr = Hash(key);
    while (H[addr].value != key)
    {
        addr = (addr + 1) % HASHSIZE;
        if (H[addr].count == 0 || addr == Hash(key))
        {
            return -1;
        }
    }

    return addr;
}

void PrintDir(int addr)
{
    printf("New (hardlink==%d):\n", H[addr].count);
    char *temp;
    for (int i = 0; i < H[addr].count; i++)
    {
        printf("i = %d, dir = %s\n", i, H[addr].dir[i]);
        temp = H[addr].dir[i];
        free(temp);
    }
    printf("\n");
    H[addr].count = 0;
    H[addr].value = NULLKEY;

    return;
}

static void TravelDir(char *path)
{
    DIR *d = NULL;
    struct dirent *dp = NULL; /* readdir函数的返回值就存放在这个结构体中 */
    struct stat st;
    char *p = NULL;

    if (lstat(path, &st) < 0 || !S_ISDIR(st.st_mode))
    {
        printf("invalid path: %s\n", path);
        return;
    }

    if (!(d = opendir(path)))
    {
        printf("opendir[%s] error.\n", path);
        return;
    }

    while ((dp = readdir(d)) != NULL)
    {
        /* 把当前目录.，上一级目录..及隐藏文件都去掉，避免死循环遍历目录 */
        if ((!strncmp(dp->d_name, ".", 1)) || (!strncmp(dp->d_name, "..", 2)))
            continue;
        p = (char *)malloc(sizeof(char) * MAX_PATH_LEN);
        snprintf(p, MAX_PATH_LEN - 1, "%s/%s", path, dp->d_name);
        lstat(p, &st);
        // printf("p = %s\n", p);
        if (!S_ISDIR(st.st_mode))
        {
            // if (S_ISREG(st.st_mode))
            // {
            printf("Name = %s\n", dp->d_name);
            printf("Type = %d, Hardlink = %u, Ino = %llu\n\n", st.st_mode, st.st_nlink, st.st_ino);
            if (st.st_nlink >= 2)
            {
                DirValue temp;
                temp.dir = p;
                temp.value = st.st_ino;
                int addr = SearchHashTable(temp);
                if (addr == -1)
                {
                    InsertHashTable(temp);
                }
                else
                {
                    InsertHashTable(temp);
                    // PrintDir(addr);
                }
            }
            // }
        }
        else
        {
            // printf("%s/\n", dp->d_name);
            TravelDir(p);
        }
    }
    closedir(d);

    return;
}

int main(int argc, char **argv)
{
    char *path = NULL;

    if (argc != 2)
    {
        printf("Usage: %s [dir]\n", argv[0]);
        printf("use DEFAULT option: %s .\n", argv[0]);
        printf("-------------------------------------------\n");
        path = ".";
    }
    else
    {
        path = argv[1];
    }

    InitHashTable();

    TravelDir(path);

    printf("--------------------------------\n");
    for (int i = 0; i < HASHSIZE; i++)
    {
        if (H[i].value > 1)
        {
            PrintDir(i);
        }
    }

    return 0;
}
