/* Enable the verified external output, then start a fresh rEFInd instance.
 * A separate copy owns its configuration. No firmware variable is written.
 */
#define HDMI_MENU_BUILD
#ifndef HDMI_LOG_PATH
#define HDMI_LOG_PATH L"\\EFI\\gopdiag\\hdmi-menu.txt"
#endif
#include "target-output.c"

static EFI_STATUS loadNamedMenu(EFI_HANDLE image,CHAR16 *filename,EFI_HANDLE *child) {
  EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
  UINT8 *device=NULL,*path=NULL,*node;
  UINTN length,chars=0,fileNodeLength;
  while(filename[chars] && chars<240) chars++;
  if(chars==240) return EFI_INVALID_PARAMETER;
  fileNodeLength=4+(chars+1)*sizeof(CHAR16);
  EFI_STATUS s=bs->HandleProtocol(image,&imageGuid,(VOID **)&loaded);
  if(EFI_ERROR(s)) return s;
  s=bs->HandleProtocol(loaded->DeviceHandle,&dpGuid,(VOID **)&device);
  if(EFI_ERROR(s)) return s;
  length=pathLength(device);if(length<4) return EFI_UNSUPPORTED;
  s=bs->AllocatePool(EfiBootServicesData,length+fileNodeLength,(VOID **)&path);
  if(EFI_ERROR(s)) return s;
  bs->CopyMem(path,device,length-4);
  node=path+length-4;
  node[0]=MEDIA_DEVICE_PATH;node[1]=MEDIA_FILEPATH_DP;
  node[2]=(UINT8)fileNodeLength;node[3]=(UINT8)(fileNodeLength>>8);
  bs->CopyMem(node+4,filename,(chars+1)*sizeof(CHAR16));
  node+=fileNodeLength;node[0]=0x7f;node[1]=0xff;node[2]=4;node[3]=0;
  s=bs->LoadImage(FALSE,image,(EFI_DEVICE_PATH_PROTOCOL *)path,NULL,0,child);
  bs->FreePool(path);return s;
}
static EFI_STATUS loadMenu(EFI_HANDLE image,EFI_HANDLE *child) {
  return loadNamedMenu(image,L"\\EFI\\refind-hdmi\\refind_x64.efi",child);
}

static EFI_STATUS set1080(EFI_GRAPHICS_OUTPUT_PROTOCOL *g) {
  UINT32 i;
  if(!g || !g->Mode || !g->Mode->MaxMode || g->Mode->MaxMode>1024) return EFI_UNSUPPORTED;
  for(i=0;i<g->Mode->MaxMode;i++) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *details=NULL;
    UINTN size=0;BOOLEAN matches=FALSE;
    EFI_STATUS s=g->QueryMode(g,i,&size,&details);
    if(!EFI_ERROR(s) && details && size>=sizeof(*details))
      matches=(details->HorizontalResolution==1920 && details->VerticalResolution==1080);
    if(details) bs->FreePool(details);
    if(matches) return g->SetMode(g,i);
  }
  return EFI_UNSUPPORTED;
}

static VOID restoreInternal(UINT32 mode) {
  EFI_HANDLE gpu=findPath(gpuPath,sizeof(gpuPath));
  EFI_GRAPHICS_OUTPUT_PROTOCOL *g;
  if(gpu) result("Restore internal connection=",bs->ConnectController(gpu,NULL,(EFI_DEVICE_PATH_PROTOCOL *)(internalPath+24),FALSE));
  g=gopAt(internalPath,sizeof(internalPath));
  if(g && g->Mode && mode<g->Mode->MaxMode) result("Restore internal mode=",g->SetMode(g,mode));
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st) {
  EFI_HANDLE gpu,external,child=NULL;
  EFI_EDID_DISCOVERED_PROTOCOL *edid=NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *g;
  EFI_STATUS s;UINT32 originalMode;
  bs=st->BootServices;
  s=openLog(image);if(EFI_ERROR(s)) return s;
  text("=== HDMI MENU v1 BEGIN ===");emit();snapshot("BEFORE MENU SWITCH");
  gpu=findPath(gpuPath,sizeof(gpuPath));external=findPath(externalPath,sizeof(externalPath));
  g=gopAt(internalPath,sizeof(internalPath));
  if(!gpu || !external || !g || !g->Mode || g->Mode->Mode>=g->Mode->MaxMode) {
    text("Missing expected hardware or working internal output. Returning unchanged.");emit();
    logfile->Close(logfile);return EFI_NOT_FOUND;
  }
  originalMode=g->Mode->Mode;
  s=bs->HandleProtocol(external,&edidGuid,(VOID **)&edid);
  if(EFI_ERROR(s) || !edid || !edid->Edid || edid->SizeOfEdid<128) {
    text("No detected external monitor EDID. Returning unchanged.");emit();
    logfile->Close(logfile);return EFI_NOT_FOUND;
  }
  s=loadMenu(image,&child);result("Load separate rEFInd=",s);
  if(EFI_ERROR(s)) {logfile->Close(logfile);return s;}
  s=bs->ConnectController(gpu,NULL,(EFI_DEVICE_PATH_PROTOCOL *)(externalPath+24),FALSE);
  result("External connect=",s);
  g=gopAt(externalPath,sizeof(externalPath));
  if(!EFI_ERROR(s)) s=set1080(g);
  result("External 1080p=",s);
  if(EFI_ERROR(s)) {
    restoreInternal(originalMode);bs->UnloadImage(child);logfile->Close(logfile);return s;
  }
  snapshot("BEFORE STARTING HDMI MENU");
  text("Starting fresh rEFInd. If an OS boots, this log normally ends here.");emit();
  logfile->Close(logfile);logfile=NULL;
  s=bs->StartImage(child,NULL,NULL);
  if(EFI_ERROR(openLog(image))) {
    /* Even if logging fails after return, recover the original display. */
    gpu=findPath(gpuPath,sizeof(gpuPath));
    if(gpu) bs->ConnectController(gpu,NULL,(EFI_DEVICE_PATH_PROTOCOL *)(internalPath+24),FALSE);
    g=gopAt(internalPath,sizeof(internalPath));
    if(g && g->Mode && originalMode<g->Mode->MaxMode) g->SetMode(g,originalMode);
    return s;
  }
  result("Menu returned=",s);restoreInternal(originalMode);
  text("=== HDMI MENU RETURN COMPLETE ===");emit();logfile->Close(logfile);return s;
}
