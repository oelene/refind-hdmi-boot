/* Focused volatile GOP test for the observed AN515-58 device paths.
 * Uses official EDK II types. No SetVariable, DisconnectController or flash IO.
 * Unknown/missing paths cause a read-only exit. Log is append-only.
 */
#include <Uefi.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/EdidActive.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#ifndef HDMI_LOG_PATH
#define HDMI_LOG_PATH L"\\EFI\\gopdiag\\target-output.txt"
#endif

static EFI_BOOT_SERVICES *bs;
static EFI_FILE_PROTOCOL *logfile;
static EFI_GUID dpGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
/* Keep a real relocation so firmware loaders can relocate this image. */
static EFI_GUID *volatile relocatableDpGuid = &dpGuid;
static EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID edidGuid = EFI_EDID_DISCOVERED_PROTOCOL_GUID;
static EFI_GUID activeGuid = EFI_EDID_ACTIVE_PROTOCOL_GUID;
static EFI_GUID imageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static CHAR8 line[2048];
static UINTN used;

/* Observed paths: root0 / PCI1,0 / PCI0,0 / ACPI_ADR. */
__declspec(align(8)) static UINT8 gpuPath[] = {
  2,1,12,0,0xd0,0x41,3,0x0a,0,0,0,0,
  1,1,6,0,0,1, 1,1,6,0,0,0, 0x7f,0xff,4,0
};
__declspec(align(8)) static UINT8 internalPath[] = {
  2,1,12,0,0xd0,0x41,3,0x0a,0,0,0,0,
  1,1,6,0,0,1, 1,1,6,0,0,0,
  2,3,8,0,0,4,1,0x80, 0x7f,0xff,4,0
};
__declspec(align(8)) static UINT8 externalPath[] = {
  2,1,12,0,0xd0,0x41,3,0x0a,0,0,0,0,
  1,1,6,0,0,1, 1,1,6,0,0,0,
  2,3,8,0,0x30,3,1,0x80, 0x7f,0xff,4,0
};

static VOID text(CONST CHAR8 *s) {
  while (*s && used < sizeof(line)-3) line[used++] = *s++;
}
static VOID hex(UINT64 n) {
  UINTN i;
  text("0x");
  for(i=0;i<16;i++) {
    UINTN shift = (15-i)*4;
    if(used < sizeof(line)-3) line[used++] = "0123456789ABCDEF"[(n>>shift)&15];
  }
}
static VOID emit(VOID) {
  UINTN bytes;
  if(!logfile) {used=0;return;}
  line[used++]='\r'; line[used++]='\n'; bytes=used;
  logfile->Write(logfile,&bytes,line); logfile->Flush(logfile); used=0;
}
static VOID result(CONST CHAR8 *label, EFI_STATUS status) {
  text(label); hex(status); emit();
}
static UINTN pathLength(CONST UINT8 *p) {
  UINTN off=0,n;
  if(!p) return 0;
  while(off<=508) {
    n=(UINTN)p[off+2]+((UINTN)p[off+3]<<8);
    if(n<4 || off+n>512) return 0;
    if(p[off]==0x7f) return (p[off+1]==0xff && n==4) ? off+4 : 0;
    off+=n;
  }
  return 0;
}
static BOOLEAN same(CONST UINT8 *a, CONST UINT8 *b, UINTN n) {
  UINTN i; for(i=0;i<n;i++) if(a[i]!=b[i]) return FALSE; return TRUE;
}
static EFI_HANDLE findPath(UINT8 *wanted, UINTN size) {
  EFI_HANDLE *handles=NULL,found=NULL;
  UINTN count=0,i; UINT8 *path=NULL;
  if(EFI_ERROR(bs->LocateHandleBuffer(ByProtocol,relocatableDpGuid,NULL,&count,&handles))) return NULL;
  for(i=0;i<count;i++) {
    if(!EFI_ERROR(bs->HandleProtocol(handles[i],&dpGuid,(VOID **)&path)) &&
       pathLength(path)==size && same(path,wanted,size)) {
      if(found) { found=NULL; break; } /* ambiguous: do not act */
      found=handles[i];
    }
  }
  bs->FreePool(handles); return found;
}
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gopAt(UINT8 *path, UINTN size) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL; EFI_HANDLE h=findPath(path,size);
  if(h) bs->HandleProtocol(h,&gopGuid,(VOID **)&g); return g;
}
static VOID dumpPath(EFI_HANDLE h) {
  UINT8 *p=NULL; UINTN i,n;
  text("path=");
  if(EFI_ERROR(bs->HandleProtocol(h,&dpGuid,(VOID **)&p))) {text("none"); emit(); return;}
  n=pathLength(p);
  for(i=0;i<n && used+3<sizeof(line);i++) {
    line[used++]="0123456789ABCDEF"[p[i]>>4];
    line[used++]="0123456789ABCDEF"[p[i]&15]; line[used++]=' ';
  }
  emit();
}
static VOID dumpEdid(EFI_HANDLE h, EFI_GUID *guid, CONST CHAR8 *label) {
  EFI_EDID_DISCOVERED_PROTOCOL *e=NULL; UINTN i,cap;
  EFI_STATUS s=bs->HandleProtocol(h,guid,(VOID **)&e);
  text(label); hex(s);
  if(!EFI_ERROR(s) && e) {
    text(" bytes="); hex(e->SizeOfEdid); text(" data=");
    cap=e->SizeOfEdid; if(cap>256) cap=256;
    if(e->Edid) for(i=0;i<cap && used+3<sizeof(line);i++) {
      line[used++]="0123456789ABCDEF"[e->Edid[i]>>4];
      line[used++]="0123456789ABCDEF"[e->Edid[i]&15];
    }
  }
  emit();
}
static VOID snapshot(CONST CHAR8 *label) {
  EFI_HANDLE *handles=NULL; UINTN count=0,i;
  text(label); emit();
  if(EFI_ERROR(bs->LocateHandleBuffer(AllHandles,NULL,NULL,&count,&handles))) return;
  for(i=0;i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;
    VOID *e=NULL;
    EFI_STATUS gs=bs->HandleProtocol(handles[i],&gopGuid,(VOID **)&g);
    EFI_STATUS es=bs->HandleProtocol(handles[i],&edidGuid,&e);
    if(EFI_ERROR(gs) && EFI_ERROR(es)) continue;
    text("handle="); hex((UINTN)handles[i]); emit(); dumpPath(handles[i]);
    dumpEdid(handles[i],&edidGuid,"EDID discovered status=");
    dumpEdid(handles[i],&activeGuid,"EDID active status=");
    if(!EFI_ERROR(gs) && g && g->Mode) {
      text("GOP mode="); hex(g->Mode->Mode); text(" count="); hex(g->Mode->MaxMode);
      text(" framebuffer="); hex(g->Mode->FrameBufferBase);
      if(g->Mode->Info) {
        text(" width="); hex(g->Mode->Info->HorizontalResolution);
        text(" height="); hex(g->Mode->Info->VerticalResolution);
      }
      emit();
    }
  }
  bs->FreePool(handles);
}
static EFI_STATUS openLog(EFI_HANDLE image) {
  EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
  EFI_FILE_PROTOCOL *root=NULL; EFI_STATUS s;
  logfile=NULL;
  s=bs->HandleProtocol(image,&imageGuid,(VOID **)&loaded); if(EFI_ERROR(s)) return s;
  s=bs->HandleProtocol(loaded->DeviceHandle,&fsGuid,(VOID **)&fs); if(EFI_ERROR(s)) return s;
  s=fs->OpenVolume(fs,&root); if(EFI_ERROR(s)) return s;
  s=root->Open(root,&logfile,HDMI_LOG_PATH,
    EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
  root->Close(root);
  if(EFI_ERROR(s)) return s;
  s=logfile->SetPosition(logfile,MAX_UINT64);
  if(EFI_ERROR(s)) {logfile->Close(logfile);logfile=NULL;}
  return s;
}

#ifndef HDMI_MENU_BUILD
static VOID testModes(EFI_GRAPHICS_OUTPUT_PROTOCOL *g) {
  UINT32 widths[3]={1920,2560,640}, heights[3]={1080,1440,480};
  UINT32 selected[3]={MAX_UINT32,MAX_UINT32,MAX_UINT32};
  UINT32 mode,phase;
  for(mode=0;mode<g->Mode->MaxMode;mode++) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *details=NULL;
    UINTN size=0;
    EFI_STATUS s=g->QueryMode(g,mode,&size,&details);
    text("Query mode=");hex(mode);text(" status=");hex(s);
    if(!EFI_ERROR(s) && details && size>=sizeof(*details)) {
      text(" width=");hex(details->HorizontalResolution);
      text(" height=");hex(details->VerticalResolution);
      text(" pixel_format=");hex(details->PixelFormat);
      for(phase=0;phase<3;phase++)
        if(details->HorizontalResolution==widths[phase] && details->VerticalResolution==heights[phase] &&
           selected[phase]==MAX_UINT32) selected[phase]=mode;
    }
    emit(); if(details) bs->FreePool(details);
  }
  for(phase=0;phase<3;phase++) {
    EFI_STATUS s;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL color;
    text("PHASE=");hex(phase+1);text(" requested_width=");hex(widths[phase]);
    text(" requested_height=");hex(heights[phase]);text(" selected_mode=");hex(selected[phase]);emit();
    if(selected[phase]==MAX_UINT32) {text("SKIPPED: mode not advertised.");emit();continue;}
    s=g->SetMode(g,selected[phase]);result("External SetMode status=",s);
    if(EFI_ERROR(s) || !g->Mode || !g->Mode->Info) continue;
    color.Blue=(phase==0)?200:20;
    color.Green=(phase==1)?200:20;
    color.Red=(phase==2)?200:20; color.Reserved=0;
    s=g->Blt(g,&color,EfiBltVideoFill,0,0,0,0,
      g->Mode->Info->HorizontalResolution,g->Mode->Info->VerticalResolution,0);
    result("External color fill status=",s);
    text("Holding this mode for 15 seconds.");emit();
    bs->Stall(15000000);
  }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  EFI_HANDLE gpu,external;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *original,*g;
  EFI_STATUS s; UINT32 oldMode;
  bs=st->BootServices;
  s=openLog(image);
  if(EFI_ERROR(s)) {
    st->ConOut->OutputString(st->ConOut,L"Cannot open diagnostic log. No display changes made.\r\n");
    return s;
  }
  text("=== TARGET OUTPUT TEST v2 BEGIN ==="); emit(); snapshot("BEFORE");
  gpu=findPath(gpuPath,sizeof(gpuPath));
  external=findPath(externalPath,sizeof(externalPath));
  original=gopAt(internalPath,sizeof(internalPath));
  if(!gpu || !external || !original || !original->Mode || !original->Mode->Info ||
     original->Mode->Mode>=original->Mode->MaxMode) {
    text("SKIPPED: expected GPU, DFP3 and working internal GOP were not all found."); emit();
    logfile->Close(logfile); return EFI_NOT_FOUND;
  }
  oldMode=original->Mode->Mode;
  st->ConOut->OutputString(st->ConOut,L"HDMI v2: 1080p BLUE, 1440p GREEN, 640x480 RED; 15 seconds each.\r\nInternal screen may go dark. Wait up to 60 seconds for automatic restore.\r\n");
  text("Calling ConnectController with existing DFP3 ADR; recursive FALSE."); emit();
  s=bs->ConnectController(gpu,NULL,(EFI_DEVICE_PATH_PROTOCOL *)(externalPath+24),FALSE);
  result("External ConnectController status=",s); snapshot("AFTER TARGET CONNECT");
  g=gopAt(externalPath,sizeof(externalPath));
  if(g && g->Mode && g->Mode->MaxMode && g->Mode->MaxMode<1024) {
    testModes(g);
  } else {text("No standalone GOP appeared on DFP3."); emit();}
  /* No disconnect: request the previously active internal output again. */
  gpu=findPath(gpuPath,sizeof(gpuPath));
  if(gpu) {
    s=bs->ConnectController(gpu,NULL,(EFI_DEVICE_PATH_PROTOCOL *)(internalPath+24),FALSE);
    result("Internal restore ConnectController status=",s);
  }
  original=gopAt(internalPath,sizeof(internalPath));
  if(original && original->Mode && oldMode<original->Mode->MaxMode) {
    s=original->SetMode(original,oldMode); result("Internal restore SetMode status=",s);
  } else {text("Internal restore GOP unavailable; firmware restart may be needed."); emit();}
  snapshot("AFTER RESTORE"); text("=== TARGET OUTPUT TEST COMPLETE ==="); emit();
  logfile->Close(logfile); return EFI_SUCCESS;
}
#endif
