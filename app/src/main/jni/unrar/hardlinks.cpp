bool ExtractHardlink(wchar *NameNew,wchar *NameExisting,size_t NameExistingSize)
{
#ifndef NOFILECREATE
  SlashToNative(NameExisting,NameExisting,NameExistingSize); // Not needed for RAR 5.1+ archives.

  if (!FileExist(NameExisting))
    return false;
  CreatePath(NameNew,true);

#ifdef _WIN_ALL
  bool Success=CreateHardLink(NameNew,NameExisting,NULL)!=0;
  if (!Success)
  {
    uiMsg(UIERROR_HLINKCREATE,NameNew);
    ErrHandler.SysErrMsg();
    ErrHandler.SetErrorCode(RARX_CREATE);
  }
  return Success;
#elif defined(_UNIX)
  char NameExistingA[NM],NameNewA[NM];
  WideToChar(NameExisting,NameExistingA,ASIZE(NameExistingA));
  WideToChar(NameNew,NameNewA,ASIZE(NameNewA));
  bool Success=link(NameExistingA,NameNewA)==0;
  if (!Success)
  {
    uiMsg(UIERROR_HLINKCREATE,NameNew);
    ErrHandler.SysErrMsg();
    ErrHandler.SetErrorCode(RARX_CREATE);
  }
  return Success;
#else
  return false;
#endif
#else
  return false;
#endif
}
