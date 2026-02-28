#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <fileapi.h>
#include "windows/sysstat.h"
#endif

extern "C"
{
  void *gc_alloc(int size);

  bool runtime_file_exists(char *fpath) {
    struct stat buffer;
    return (stat(fpath, &buffer) == 0);
  }

  int runtime_get_file_mode(char *fpath)
  {
    if (!runtime_file_exists(fpath))
    {
      return false;
    }

    struct stat buf;
    stat(fpath, &buf);

    return buf.st_mode;
  }

  bool runtime_file_chmod(char *fpath, int mode)
  {
    if (!runtime_file_exists(fpath))
    {
      return false;
    }

#ifndef _WIN32
    return chmod(fpath, mode);
#else
    return false;
#endif
  }

  bool runtime_file_is_directory(char *fpath)
  {
    if (!runtime_file_exists(fpath))
    {
      return false;
    }

    struct stat buf;
    stat(fpath, &buf);

    return S_ISDIR(buf.st_mode);
  }

  bool runtime_file_is_regular(char *fpath)
  {
    if (!runtime_file_exists(fpath))
    {
      return false;
    }

    struct stat buf;
    stat(fpath, &buf);

    return S_ISREG(buf.st_mode);
  }

  bool runtime_file_is_link(char *fpath)
  {
    if (!runtime_file_exists(fpath))
    {
      return false;
    }

    struct stat buf;
    stat(fpath, &buf);

    return S_ISLNK(buf.st_mode);
  }

  bool runtime_file_is_block(char *fpath)
  {
    if (!runtime_file_exists(fpath))
    {
      return false;
    }

    struct stat buf;
    stat(fpath, &buf);

    return S_ISBLK(buf.st_mode);
  }

  bool runtime_file_is_fifo_pipe(char *fpath)
  {
    if (!runtime_file_exists(fpath))
    {
      return false;
    }

    struct stat buf;
    stat(fpath, &buf);

    return S_ISFIFO(buf.st_mode);
  }

  bool check_file_change(int og_length, char *og_text, char *fpath)
  {
    FILE *fptr;
    fptr = fopen(fpath, "r");

    if (fptr == NULL)
    {
      return NULL;
    }

    char i;
    int size = 0;
    while ((i = fgetc(fptr)) != EOF)
    {
      if (size >= og_length)
      {
        return false;
      }

      if (i != og_text[size])
      {
        return false;
      }

      size += 1;
    }

    fclose(fptr);

    return size == og_length;
  }

  char *runtime_read_file(char *fpath)
  {
    FILE *fptr;
    fptr = fopen(fpath, "r");

    if (fptr == NULL)
    {
      return NULL;
    }

    char _;
    int size = 0;
    while ((_ = fgetc(fptr)) != EOF)
    {
      size += 1;
    }

    fclose(fptr);
    fptr = fopen(fpath, "r");

    char *string = (char *)gc_alloc(sizeof(char) * size);

    char c;
    int i = 0;
    while ((c = fgetc(fptr)) != EOF)
    {
      string[i] = c;
      i += 1;
    }

    fclose(fptr);

    return string;
  }

  bool runtime_write_file(char *fpath, char *text)
  {
    FILE *fptr;
    fptr = fopen(fpath, "w");
    if (fptr == NULL)
    {
      return false;
    }

    int push_result = fprintf(fptr, "%s", text);

    fclose(fptr);

    return push_result;
  }

  bool runtime_append_file(char *fpath, char *text)
  {
    FILE *fptr;
    fptr = fopen(fpath, "a");
    if (fptr == NULL)
    {
      return false;
    }

    int push_result = fprintf(fptr, "%s", text);

    fclose(fptr);

    return push_result;
  }

  bool runtime_make_directory(char *dirpath)
  {
#ifndef _WIN32
    return mkdir(dirpath, 0777);
#else
    CreateDirectory(dirpath, NULL);
#endif
  }

  bool runtime_file_remove(char *fpath)
  {
    return remove(fpath) == 0;
  }

#ifndef _WIN32
#include <dirent.h>
  int get_directory_child_count(char *dirpath)
  {
    int fileamount = 0;

    struct dirent *de;

    DIR *dr = opendir(dirpath);

    if (dr == NULL)
    {
      return 0;
    }

    while ((de = readdir(dr)) != NULL)
    {
      if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
      {
        fileamount += 1;
      }
    }

    closedir(dr);

    return fileamount;
  }

  char **list_directory(char *dirpath)
  {
    char **files_list = (char **)gc_alloc(sizeof(char *) * get_directory_child_count(dirpath));

    int fileamount = 0;

    struct dirent *de;

    DIR *dr = opendir(dirpath);

    if (dr == NULL)
    {
      return 0;
    }

    int i = 0;
    while ((de = readdir(dr)) != NULL)
    {
      if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
      {
        files_list[i] = de->d_name;
        i += 1;
      }
    }
    closedir(dr);

    return files_list;
  }
#else
  int get_directory_child_count(char *sDir)
  {
    WIN32_FIND_DATA fdFile;
    HANDLE hFind = NULL;

    char sPath[2048];

    sprintf(sPath, "%s\\*.*", sDir);

    if ((hFind = FindFirstFile(sPath, &fdFile)) == INVALID_HANDLE_VALUE)
    {
      return 0;
    }

    int childcount = 0;
    do
    {
      if (strcmp(fdFile.cFileName, ".") != 0 && strcmp(fdFile.cFileName, "..") != 0)
      {
        childcount += 1;
      }
    } while (FindNextFile(hFind, &fdFile));

    FindClose(hFind);

    return childcount;
  }

  char **list_directory(char *sDir)
  {
    WIN32_FIND_DATA fdFile;
    HANDLE hFind = NULL;

    char sPath[2048];

    sprintf(sPath, "%s\\*.*", sDir);

    if ((hFind = FindFirstFile(sPath, &fdFile)) == INVALID_HANDLE_VALUE)
    {
      return 0;
    }

    char **files_list = (char **)gc_alloc(sizeof(char *) * get_directory_child_count(sDir));

    int i = 0;
    do
    {
      if (strcmp(fdFile.cFileName, ".") != 0 && strcmp(fdFile.cFileName, "..") != 0)
      {
        files_list[i] = sPath;
        i += 1;
      }
    } while (FindNextFile(hFind, &fdFile));

    FindClose(hFind);

    return files_list;
  }

  extern "C"
  {
    void windows_hide_console()
    {
      HWND console_window = GetConsoleWindow(); // window handle
      ShowWindow(console_window, 0);
    }
  }
#endif
}