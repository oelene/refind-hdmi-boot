/* Automatic HDMI selection with the original menu as an independent fallback. */
#define HDMI_LOG_PATH L"\\EFI\\gopdiag\\hdmi-auto.txt"
#define efi_main hdmi_manual_entry
#include "hdmi-menu.c"
#undef efi_main

static VOID closeLog(VOID) {
  if(logfile) {logfile->Close(logfile);logfile=NULL;}
}
static EFI_STATUS startOriginal(EFI_HANDLE image) {
  EFI_HANDLE child=NULL;
  EFI_STATUS s=loadNamedMenu(image,L"\\EFI\\refind\\refind_x64.efi",&child);
  result("Load original fallback=",s);closeLog();
  if(!EFI_ERROR(s)) s=bs->StartImage(child,NULL,NULL);
  return s;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st) {
  EFI_HANDLE gpu,external,child=NULL;
  EFI_EDID_DISCOVERED_PROTOCOL *edid=NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *g;
  UINT32 originalMode=0;
  BOOLEAN attempted=FALSE;
  EFI_STATUS s;
  bs=st->BootServices;
  /* Logging is optional: an unavailable/full ESP must not block the menu. */
  openLog(image);
  text("=== HDMI AUTO v1 BEGIN ===");emit();
  s=loadMenu(image,&child);result("Load primary menu=",s);
  if(EFI_ERROR(s)) return startOriginal(image);
  gpu=findPath(gpuPath,sizeof(gpuPath));external=findPath(externalPath,sizeof(externalPath));
  g=gopAt(internalPath,sizeof(internalPath));
  if(gpu && external && g && g->Mode && g->Mode->Info && g->Mode->Mode<g->Mode->MaxMode &&
     !EFI_ERROR(bs->HandleProtocol(external,&edidGuid,(VOID **)&edid)) &&
     edid && edid->Edid && edid->SizeOfEdid>=128) {
    originalMode=g->Mode->Mode;attempted=TRUE;
    s=bs->ConnectController(gpu,NULL,(EFI_DEVICE_PATH_PROTOCOL *)(externalPath+24),FALSE);
    result("External connect=",s);
    if(!EFI_ERROR(s)) s=set1080(gopAt(externalPath,sizeof(externalPath)));
    result("External 1080p=",s);
    if(EFI_ERROR(s)) {restoreInternal(originalMode);attempted=FALSE;}
  } else {
    text("No eligible HDMI output; retaining firmware display.");emit();
  }
  text("Starting three-system menu.");emit();closeLog();
  s=bs->StartImage(child,NULL,NULL);
  /* Normal OS handoff never returns. Exit/error goes to the original menu. */
  openLog(image);result("Primary menu returned=",s);
  if(attempted) restoreInternal(originalMode);
  return startOriginal(image);
}
