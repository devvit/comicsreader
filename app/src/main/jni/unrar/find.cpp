#include "rar.hpp"

FindFile::FindFile()
{
  *FindMask=0;
  FirstCall=true;
#ifdef _WIN_ALL
  hFind=INVALID_HANDLE_VALUE;
#else
  dirp=NULL;
#endif
}


FindFile::~FindFile()
{
#ifdef _WIN_ALL
  if (hFind!=INVALID_HANDLE_VALUE)
    FindClose(hFind);
#else
  if (dirp!=NULL)
    closedir(dirp);
#endif
}


void FindFile::SetMask(const wchar *Mask)
{
  unrar_wcscpy(FindMask,Mask);
  FirstCall=true;
}


bool FindFile::Next(FindData *fd,bool GetSymLink)
{
  fd->Error=false;
  if (*FindMask==0)
    return false;
#ifdef _WIN_ALL
  if (FirstCall)
  {
    if ((hFind=Win32Find(INVALID_HANDLE_VALUE,FindMask,fd))==INVALID_HANDLE_VALUE)
      return false;
  }
  else
    if (Win32Find(hFind,FindMask,fd)==INVALID_HANDLE_VALUE)
      return false;
#else
  if (FirstCall)
  {
    wchar DirName[NM];
    wcsncpyz(DirName,FindMask,ASIZE(DirName));
    RemoveNameFromPath(DirName);
    if (*DirName==0)
      unrar_wcscpy(DirName,L".");
    char DirNameA[NM];
    WideToChar(DirName,DirNameA,ASIZE(DirNameA));
    if ((dirp=opendir(DirNameA))==NULL)
    {
      fd->Error=(errno!=ENOENT);
      return false;
    }
  }
  while (1)
  {
    struct dirent *ent=readdir(dirp);
    if (ent==NULL)
      return false;
    if (strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0)
      continue;
    wchar Name[NM];
    if (!CharToWide(ent->d_name,Name,ASIZE(Name)))
      uiMsg(UIERROR_INVALIDNAME,UINULL,Name);

    if (CmpName(FindMask,Name,MATCH_NAMES))
    {
      wchar FullName[NM];
      unrar_wcscpy(FullName,FindMask);
      *PointToName(FullName)=0;
      if (unrar_wcslen(FullName)+unrar_wcslen(Name)>=ASIZE(FullName)-1)
      {
        uiMsg(UIERROR_PATHTOOLONG,FullName,L"",Name);
        return false;
      }
      unrar_wcscat(FullName,Name);
      if (!FastFind(FullName,fd,GetSymLink))
      {
        ErrHandler.OpenErrorMsg(FullName);
        continue;
      }
      unrar_wcscpy(fd->Name,FullName);
      break;
    }
  }
#endif
  fd->Flags=0;
  fd->IsDir=IsDir(fd->FileAttr);
  fd->IsLink=IsLink(fd->FileAttr);

  FirstCall=false;
  wchar *NameOnly=PointToName(fd->Name);
  if (unrar_wcscmp(NameOnly,L".")==0 || unrar_wcscmp(NameOnly,L"..")==0)
    return Next(fd);
  return true;
}


bool FindFile::FastFind(const wchar *FindMask,FindData *fd,bool GetSymLink)
{
  fd->Error=false;
#ifndef _UNIX
  if (IsWildcard(FindMask))
    return false;
#endif    
#ifdef _WIN_ALL
  HANDLE hFind=Win32Find(INVALID_HANDLE_VALUE,FindMask,fd);
  if (hFind==INVALID_HANDLE_VALUE)
    return false;
  FindClose(hFind);
#else
  char FindMaskA[NM];
  WideToChar(FindMask,FindMaskA,ASIZE(FindMaskA));

  struct stat st;
  if (GetSymLink)
  {
#ifdef SAVE_LINKS
    if (lstat(FindMaskA,&st)!=0)
#else
    if (stat(FindMaskA,&st)!=0)
#endif
    {
      fd->Error=(errno!=ENOENT);
      return false;
    }
  }
  else
    if (stat(FindMaskA,&st)!=0)
    {
      fd->Error=(errno!=ENOENT);
      return false;
    }
  fd->FileAttr=st.st_mode;
  fd->Size=st.st_size;
  fd->mtime=st.st_mtime;
  fd->atime=st.st_atime;
  fd->ctime=st.st_ctime;
  wcsncpyz(fd->Name,FindMask,ASIZE(fd->Name));
#endif
  fd->Flags=0;
  fd->IsDir=IsDir(fd->FileAttr);
  fd->IsLink=IsLink(fd->FileAttr);

  return true;
}


#ifdef _WIN_ALL
HANDLE FindFile::Win32Find(HANDLE hFind,const wchar *Mask,FindData *fd)
{
  WIN32_FIND_DATA FindData;
  if (hFind==INVALID_HANDLE_VALUE)
  {
    hFind=FindFirstFile(Mask,&FindData);
    if (hFind==INVALID_HANDLE_VALUE)
    {
      wchar LongMask[NM];
      if (GetWinLongPath(Mask,LongMask,ASIZE(LongMask)))
        hFind=FindFirstFile(LongMask,&FindData);
    }
    if (hFind==INVALID_HANDLE_VALUE)
    {
      int SysErr=GetLastError();
      fd->Error=(SysErr!=ERROR_FILE_NOT_FOUND &&
                 SysErr!=ERROR_PATH_NOT_FOUND &&
                 SysErr!=ERROR_NO_MORE_FILES);
    }
  }
  else
    if (!FindNextFile(hFind,&FindData))
    {
      hFind=INVALID_HANDLE_VALUE;
      fd->Error=GetLastError()!=ERROR_NO_MORE_FILES;
    }

  if (hFind!=INVALID_HANDLE_VALUE)
  {
    wcsncpyz(fd->Name,Mask,ASIZE(fd->Name));
    SetName(fd->Name,FindData.cFileName,ASIZE(fd->Name));
    fd->Size=INT32TO64(FindData.nFileSizeHigh,FindData.nFileSizeLow);
    fd->FileAttr=FindData.dwFileAttributes;
    fd->ftCreationTime=FindData.ftCreationTime;
    fd->ftLastAccessTime=FindData.ftLastAccessTime;
    fd->ftLastWriteTime=FindData.ftLastWriteTime;
    fd->mtime=FindData.ftLastWriteTime;
    fd->ctime=FindData.ftCreationTime;
    fd->atime=FindData.ftLastAccessTime;


  }
  fd->Flags=0;
  return hFind;
}
#endif

