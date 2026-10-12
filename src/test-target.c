#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#undef NULL
#ifdef HDMI_AUTO_TEST
#include "hdmi-auto.c"
#elif defined(HDMI_MENU_TEST)
#include "hdmi-menu.c"
#else
#include "target-output.c"
#endif

static EFI_BOOT_SERVICES mockBs;
static EFI_SYSTEM_TABLE mockSt;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console;
static EFI_LOADED_IMAGE_PROTOCOL loaded;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fs;
static EFI_FILE_PROTOCOL file;
static EFI_GRAPHICS_OUTPUT_PROTOCOL internalGop, externalGop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE internalMode, externalMode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION externalInfo;
static const UINT32 testWidths[3]={2560,640,1920}, testHeights[3]={1440,480,1080};
static int queryFails,modeFails;
static int loadFails,noEdid,loads,starts,unloads;
static int logFails,startFails;
static UINT8 mockEdidBytes[128];
static EFI_EDID_DISCOVERED_PROTOCOL mockEdid={128,mockEdidBytes};
static int missing, targetAvailable, failConnect, connected, connects, sets, fills, stalls;
static int sameGuid(EFI_GUID *a,EFI_GUID *b) {return same((UINT8 *)a,(UINT8 *)b,sizeof(*a));}
static EFI_STATUS EFIAPI handle(EFI_HANDLE h,EFI_GUID *guid,VOID **out) {
  UINTN id=(UINTN)h;
  *out=NULL;
  if(sameGuid(guid,&dpGuid)) {
    if(id==1) *out=gpuPath;
    if(id==2) *out=internalPath;
    if(id==3 && !missing) *out=externalPath;
    if(id==5) *out=gpuPath;
  }
  if(sameGuid(guid,&gopGuid)) {
    if(id==2) *out=&internalGop;
    if(id==3 && targetAvailable && connected) *out=&externalGop;
  }
  if(sameGuid(guid,&imageGuid) && id==4) *out=&loaded;
  if(sameGuid(guid,&fsGuid) && id==5) *out=&fs;
  if(sameGuid(guid,&edidGuid) && id==3 && !noEdid) *out=&mockEdid;
  return *out?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI locate(EFI_LOCATE_SEARCH_TYPE t,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **out) {
  UINTN i; (void)t;(void)g;(void)key;
  *n=3; *out=malloc(3*sizeof(EFI_HANDLE)); assert(*out);
  for(i=0;i<3;i++) (*out)[i]=(EFI_HANDLE)(i+1); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI pool(VOID *p) {free(p);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI connect(EFI_HANDLE h,EFI_HANDLE *drivers,EFI_DEVICE_PATH_PROTOCOL *path,BOOLEAN recursive) {
  assert(h==(EFI_HANDLE)1 && !drivers && !recursive);
  connects++;
  if(connects==1) {assert(same((UINT8 *)path,externalPath+24,12));connected=!failConnect;}
  else {assert(connects==2);assert(same((UINT8 *)path,internalPath+24,12));connected=0;}
  return failConnect?EFI_UNSUPPORTED:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI setMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *g,UINT32 mode) {
  assert(g==&internalGop || (g==&externalGop && connected)); sets++;
  if(g==&internalGop) {assert(mode==0);return EFI_SUCCESS;}
  assert(mode<3);
  if(modeFails && mode==2) return EFI_DEVICE_ERROR;
  externalInfo.HorizontalResolution=testWidths[mode];externalInfo.VerticalResolution=testHeights[mode];
  externalMode.Mode=mode;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI query(EFI_GRAPHICS_OUTPUT_PROTOCOL *g,UINT32 mode,UINTN *size,EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **out) {
  assert(g==&externalGop && mode<3);
  if(queryFails) return EFI_DEVICE_ERROR;
  *size=sizeof(**out);*out=calloc(1,*size);assert(*out);
  (*out)->HorizontalResolution=testWidths[mode];(*out)->VerticalResolution=testHeights[mode];
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *g,EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixel,
 EFI_GRAPHICS_OUTPUT_BLT_OPERATION op,UINTN sx,UINTN sy,UINTN dx,UINTN dy,UINTN w,UINTN h,UINTN delta) {
  (void)sx;(void)sy;(void)dx;(void)dy;(void)delta;
  assert(g==&externalGop && op==EfiBltVideoFill);
  assert(w==testWidths[externalMode.Mode] && h==testHeights[externalMode.Mode]);
  if(w==1920) assert(pixel->Blue==200);
  if(w==2560) assert(pixel->Green==200);
  if(w==640) assert(pixel->Red==200);
  fills++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI stall(UINTN us) {assert(us==15000000);stalls++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self,CHAR16 *s) {(void)self;(void)s;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *self,EFI_FILE_PROTOCOL **out) {(void)self;*out=&file;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI openFile(EFI_FILE_PROTOCOL *self,EFI_FILE_PROTOCOL **out,CHAR16 *name,UINT64 mode,UINT64 attrs) {
  (void)self;(void)name;(void)mode;(void)attrs;
  if(logFails) return EFI_WRITE_PROTECTED;
  *out=&file;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI closeFile(EFI_FILE_PROTOCOL *self) {(void)self;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI writeFile(EFI_FILE_PROTOCOL *self,UINTN *size,VOID *data) {(void)self;(void)size;(void)data;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI position(EFI_FILE_PROTOCOL *self,UINT64 pos) {(void)self;assert(pos==MAX_UINT64);return EFI_SUCCESS;}
#ifdef HDMI_MENU_TEST
static EFI_STATUS EFIAPI alloc(EFI_MEMORY_TYPE type,UINTN n,VOID **out) {
  assert(type==EfiBootServicesData);*out=malloc(n);return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
}
static VOID EFIAPI copy(VOID *dest,VOID *src,UINTN n) {memmove(dest,src,n);}
static EFI_STATUS EFIAPI load(BOOLEAN policy,EFI_HANDLE parent,EFI_DEVICE_PATH_PROTOCOL *dp,VOID *buf,UINTN n,EFI_HANDLE *child) {
  const CHAR16 expected[]=L"\\EFI\\refind-hdmi\\refind_x64.efi";
  UINT8 *path=(UINT8 *)dp;
#ifdef HDMI_AUTO_TEST
  const CHAR16 fallback[]=L"\\EFI\\refind\\refind_x64.efi";
  BOOLEAN isFallback=(path[26]==4+sizeof(fallback));
#endif
  assert(!policy && parent==(EFI_HANDLE)4 && !buf && n==0);
  assert(same(path,gpuPath,24));
  assert(path[24]==MEDIA_DEVICE_PATH && path[25]==MEDIA_FILEPATH_DP);
  assert(path[27]==0);
#ifdef HDMI_AUTO_TEST
  if(isFallback) {
    assert(same(path+28,(CONST UINT8 *)fallback,sizeof(fallback)));
    assert(pathLength(path)==28+4+sizeof(fallback));
    loads++;if(loadFails==2)return EFI_NOT_FOUND;*child=(EFI_HANDLE)7;return EFI_SUCCESS;
  }
#endif
  assert(path[26]==4+sizeof(expected));
  assert(same(path+28,(CONST UINT8 *)expected,sizeof(expected)));
  assert(pathLength(path)==28+4+sizeof(expected));
  loads++;if(loadFails)return EFI_NOT_FOUND;*child=(EFI_HANDLE)6;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI start(EFI_HANDLE child,UINTN *size,CHAR16 **data) {
#ifdef HDMI_AUTO_TEST
  assert(!size && !data && logfile==NULL);
  assert(child==(EFI_HANDLE)6 || child==(EFI_HANDLE)7);
  if(child==(EFI_HANDLE)7) assert(!connected);
  if(connected) assert(externalMode.Mode==2);
  starts++;return (startFails && child==(EFI_HANDLE)6)?EFI_DEVICE_ERROR:EFI_SUCCESS;
#else
  assert(child==(EFI_HANDLE)6 && !size && !data);
  assert(connected && externalMode.Mode==2 && logfile==NULL);starts++;return EFI_SUCCESS;
#endif
}
static EFI_STATUS EFIAPI unload(EFI_HANDLE child) {assert(child==(EFI_HANDLE)6);unloads++;return EFI_SUCCESS;}
#endif
int main(void) {
  int scenario;
  mockBs.HandleProtocol=handle;mockBs.LocateHandleBuffer=locate;mockBs.FreePool=pool;
  mockBs.ConnectController=connect;mockBs.Stall=stall;
  console.OutputString=output;mockSt.BootServices=&mockBs;mockSt.ConOut=&console;
  loaded.DeviceHandle=(EFI_HANDLE)5;fs.OpenVolume=volume;
  file.Open=openFile;file.Close=closeFile;file.Flush=closeFile;file.Write=writeFile;file.SetPosition=position;
  info.HorizontalResolution=1920;info.VerticalResolution=1080;
  internalMode.Info=&info;internalMode.MaxMode=1;externalMode=internalMode;
  externalMode.MaxMode=3;externalMode.Info=&externalInfo;
  internalGop.Mode=&internalMode;externalGop.Mode=&externalMode;
  internalGop.SetMode=setMode;externalGop.SetMode=setMode;externalGop.Blt=blt;externalGop.QueryMode=query;
  assert(pathLength(gpuPath)==28 && pathLength(internalPath)==36 && pathLength(externalPath)==36);
#ifdef HDMI_MENU_TEST
  mockBs.AllocatePool=alloc;mockBs.CopyMem=copy;mockBs.LoadImage=load;mockBs.StartImage=start;mockBs.UnloadImage=unload;
#ifdef HDMI_AUTO_TEST
  for(scenario=0;scenario<11;scenario++) {
    EFI_STATUS status;
    connects=sets=fills=stalls=connected=loads=starts=unloads=0;
    missing=(scenario==0);targetAvailable=(scenario!=1);failConnect=(scenario==3);
    queryFails=(scenario==4);modeFails=(scenario==5);loadFails=(scenario==10?2:scenario==6?1:0);noEdid=(scenario==7);
    logFails=(scenario==8);startFails=(scenario==9);
    status=efi_main((EFI_HANDLE)4,&mockSt);
    assert(scenario==10?EFI_ERROR(status):status==EFI_SUCCESS);
    assert(connected==0 && loads==2);
    assert(starts==(scenario==10?0:scenario==6?1:2));
    assert(connects==((scenario==0 || scenario==6 || scenario==7 || scenario==10)?0:2));
    assert(fills==0 && stalls==0);
    printf("PASS auto scenario %d: %s\n",scenario,scenario==7?"unplugged HDMI retains internal menu":scenario==8?"unwritable log does not block boot":scenario==6?"missing primary uses original menu":scenario==10?"both missing images return failure without a loop":"menu boot and return/failure recovery verified");
  }
#else
  for(scenario=0;scenario<8;scenario++) {
    EFI_STATUS status;
    connects=sets=fills=stalls=connected=loads=starts=unloads=0;
    missing=(scenario==0);targetAvailable=(scenario!=1);failConnect=(scenario==3);
    queryFails=(scenario==4);modeFails=(scenario==5);loadFails=(scenario==6);noEdid=(scenario==7);
    status=efi_main((EFI_HANDLE)4,&mockSt);
    assert((scenario==2)?status==EFI_SUCCESS:EFI_ERROR(status));
    assert(connected==0 && starts==(scenario==2?1:0));
    assert(connects==((scenario==0 || scenario==6 || scenario==7)?0:2));
    assert(loads==((scenario==0 || scenario==7)?0:1));
    assert(unloads==((scenario==1 || scenario==3 || scenario==4 || scenario==5)?1:0));
    assert(fills==0 && stalls==0);
    printf("PASS menu scenario %d: %s\n",scenario,scenario==2?"external 1080p selected, separate image started, internal restored on return":"failed prerequisite or firmware call handled without starting menu");
  }
#endif
#else
  for(scenario=0;scenario<6;scenario++) {
    EFI_STATUS status;
    connects=sets=fills=stalls=connected=0;
    missing=(scenario==0);targetAvailable=(scenario==2 || scenario>=4);failConnect=(scenario==3);
    queryFails=(scenario==4);modeFails=(scenario==5);
    status=efi_main((EFI_HANDLE)4,&mockSt);
    assert(status==(missing?EFI_NOT_FOUND:EFI_SUCCESS));
    assert(connects==(missing?0:2));assert(connected==0);
    assert(sets==(missing?0:targetAvailable && !queryFails?4:1));
    assert(fills==(targetAvailable && !queryFails?(modeFails?2:3):0) && stalls==fills);
    printf("PASS scenario %d: %s\n",scenario,missing?"missing path exits without display changes":queryFails?"query failure skips modes and restores internal":modeFails?"failed mode skipped; later modes tested and internal restored":targetAvailable?"three modes selected by resolution, colors held, internal restored":failConnect?"unsupported target falls back safely":"no external GOP retains internal output");
  }
#endif
  return 0;
}
