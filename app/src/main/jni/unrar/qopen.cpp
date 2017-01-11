#include "rar.hpp"

QuickOpen::QuickOpen()
{
  Buf=NULL;
  Init(NULL,false);
}


QuickOpen::~QuickOpen()
{
  Close();
  delete[] Buf;
}


void QuickOpen::Init(Archive *Arc,bool WriteMode)
{
  if (Arc!=NULL) // Unless called from constructor.
    Close();

  QuickOpen::Arc=Arc;
  QuickOpen::WriteMode=WriteMode;

  ListStart=NULL;
  ListEnd=NULL;

  if (Buf==NULL)
    Buf=new byte[MaxBufSize];

  CurBufSize=0; // Current size of buffered data in write mode.

  Loaded=false;
}


void QuickOpen::Close()
{
  QuickOpenItem *Item=ListStart;
  while (Item!=NULL)
  {
    QuickOpenItem *Next=Item->Next;
    delete[] Item->Header;
    delete Item;
    Item=Next;
  }
}














void QuickOpen::Load(uint64 BlockPos)
{
  if (!Loaded) // If loading the first time, perform additional intialization.
  {
    SeekPos=Arc->Tell();
    UnsyncSeekPos=false;

    SaveFilePos SavePos(*Arc);
    Arc->Seek(BlockPos,SEEK_SET);
    if (Arc->ReadHeader()==0 || Arc->GetHeaderType()!=HEAD_SERVICE ||
        !Arc->SubHead.CmpName(SUBHEAD_TYPE_QOPEN))
      return;
    QLHeaderPos=Arc->CurBlockPos;
    RawDataStart=Arc->Tell();
    RawDataSize=Arc->SubHead.UnpSize;

    Loaded=true; // Set only after all file processing calls like Tell, Seek, ReadHeader.
  }

  if (Arc->SubHead.Encrypted)
  {
#ifndef RAR_NOCRYPT
    RAROptions *Cmd=Arc->GetRAROptions();
    if (Cmd->Password.IsSet())
      Crypt.SetCryptKeys(false,CRYPT_RAR50,&Cmd->Password,Arc->SubHead.Salt,
                         Arc->SubHead.InitV,Arc->SubHead.Lg2Count,
                         Arc->SubHead.HashKey,Arc->SubHead.PswCheck);
    else
#endif
      return;
  }

  RawDataPos=0;
  ReadBufSize=0;
  ReadBufPos=0;
  LastReadHeader.Reset();
  LastReadHeaderPos=0;

  ReadBuffer();
}


bool QuickOpen::Read(void *Data,size_t Size,size_t &Result)
{
  if (!Loaded)
    return false;
  // Find next suitable cached block.
  while (LastReadHeaderPos+LastReadHeader.Size()<=SeekPos)
    if (!ReadNext())
      break;
  if (!Loaded)
  {
    // If something wrong happened, let's set the correct file pointer
    // and stop further quick open processing.
    if (UnsyncSeekPos)
      Arc->File::Seek(SeekPos,SEEK_SET);
    return false;
  }

  if (SeekPos>=LastReadHeaderPos && SeekPos+Size<=LastReadHeaderPos+LastReadHeader.Size())
  {
    memcpy(Data,LastReadHeader+size_t(SeekPos-LastReadHeaderPos),Size);
    Result=Size;
    SeekPos+=Size;
    UnsyncSeekPos=true;
  }
  else
  {
    if (UnsyncSeekPos)
    {
      Arc->File::Seek(SeekPos,SEEK_SET);
      UnsyncSeekPos=false;
    }
    int ReadSize=Arc->File::Read(Data,Size);
    if (ReadSize<0)
    {
      Loaded=false;
      return false;
    }
    Result=ReadSize;
    SeekPos+=ReadSize;
  }
  
  return true;
}


bool QuickOpen::Seek(int64 Offset,int Method)
{
  if (!Loaded)
    return false;

  // Normally we process an archive sequentially from beginning to end,
  // so we read quick open data sequentially. But some operations like
  // archive updating involve several passes. So if we detect that file
  // pointer is moved back, we reload quick open data from beginning.
  if (Method==SEEK_SET && (uint64)Offset<SeekPos && (uint64)Offset<LastReadHeaderPos)
    Load(QLHeaderPos);

  if (Method==SEEK_SET)
    SeekPos=Offset;
  if (Method==SEEK_CUR)
    SeekPos+=Offset;
  UnsyncSeekPos=true;

  if (Method==SEEK_END)
  {
    Arc->File::Seek(Offset,SEEK_END);
    SeekPos=Arc->File::Tell();
    UnsyncSeekPos=false;
  }
  return true;
}


bool QuickOpen::Tell(int64 *Pos)
{
  if (!Loaded)
    return false;
  *Pos=SeekPos;
  return true;
}


uint QuickOpen::ReadBuffer()
{
  SaveFilePos SavePos(*Arc);
  Arc->File::Seek(RawDataStart+RawDataPos,SEEK_SET);
  size_t SizeToRead=(size_t)Min(RawDataSize-RawDataPos,MaxBufSize-ReadBufSize);
  if (Arc->SubHead.Encrypted)
    SizeToRead &= ~CRYPT_BLOCK_MASK;
  if (SizeToRead==0)
    return 0;
  int ReadSize=Arc->File::Read(Buf+ReadBufSize,SizeToRead);
  if (ReadSize<=0)
    return 0;
#ifndef RAR_NOCRYPT
  if (Arc->SubHead.Encrypted)
    Crypt.DecryptBlock(Buf+ReadBufSize,ReadSize & ~CRYPT_BLOCK_MASK);
#endif
  RawDataPos+=ReadSize;
  ReadBufSize+=ReadSize;
  return ReadSize;
}


// Fill RawRead object from buffer.
bool QuickOpen::ReadRaw(RawRead &Raw)
{
  if (MaxBufSize-ReadBufPos<0x100) // We are close to end of buffer.
  {
    // Ensure that we have enough data to read CRC and header size.
    size_t DataLeft=ReadBufSize-ReadBufPos;
    memcpy(Buf,Buf+ReadBufPos,DataLeft);
    ReadBufPos=0;
    ReadBufSize=DataLeft;
    ReadBuffer();
  }
  const size_t FirstReadSize=7;
  if (ReadBufPos+FirstReadSize>ReadBufSize)
    return false;
  Raw.Read(Buf+ReadBufPos,FirstReadSize);
  ReadBufPos+=FirstReadSize;

  uint SavedCRC=Raw.Get4();
  uint SizeBytes=Raw.GetVSize(4);
  uint64 BlockSize=Raw.GetV();
  int SizeToRead=int(BlockSize);
  SizeToRead-=FirstReadSize-SizeBytes-4; // Adjust overread size bytes if any.
  if (SizeToRead<0 || SizeBytes==0 || BlockSize==0)
  {
    Loaded=false; // Invalid data.
    return false;
  }

  // If rest of block data crosses buffer boundary, read it in loop.
  size_t DataLeft=ReadBufSize-ReadBufPos;
  while (SizeToRead>0)
  {
    size_t CurSizeToRead=Min(DataLeft,(size_t)SizeToRead);
    Raw.Read(Buf+ReadBufPos,CurSizeToRead);
    ReadBufPos+=CurSizeToRead;
    SizeToRead-=int(CurSizeToRead);
    if (SizeToRead>0) // We read the entire buffer and still need more data.
    {
      ReadBufPos=0;
      ReadBufSize=0;
      if (ReadBuffer()==0)
        return false;
    }
  }

  return SavedCRC==Raw.GetCRC50();
}


// Read next cached header.
bool QuickOpen::ReadNext()
{
  RawRead Raw(NULL);
  if (!ReadRaw(Raw)) // Read internal quick open header preceding stored block.
    return false;
  uint64 Offset=Raw.GetV();
  size_t HeaderSize=(size_t)Raw.GetV();
  LastReadHeader.Alloc(HeaderSize);
  Raw.GetB(&LastReadHeader[0],HeaderSize);
  // Calculate the absolute position as offset from quick open service header.
  LastReadHeaderPos=QLHeaderPos-Offset;
  return true;
}
