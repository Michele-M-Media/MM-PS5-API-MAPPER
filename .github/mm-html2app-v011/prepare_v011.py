from pathlib import Path
import subprocess, re

BASE_COMMIT = "f3e92fd195d45368f3fc22aee11aa7209b7721c9"
BASE_PATH = ".github/mmwebapp-v03/studio_v03.py"

s = subprocess.check_output(["git", "show", f"{BASE_COMMIT}:{BASE_PATH}"], text=True)
out = Path("buildsrc/MM-HTML2APP.py")
out.parent.mkdir(parents=True, exist_ok=True)

# Product identity.
s = s.replace('APP_TITLE = "MM PS5 WEB APP STUDIO"', 'APP_TITLE = "MM HTML2APP"')
s = s.replace('VERSION = "v0.3 WEBsrv MEDIA ALL-IN-ONE"', 'VERSION = "v0.1.1 MEDIA REGISTRATION REPAIR"')
s = s.replace('MM_WEB_APP_STUDIO', 'MM_HTML2APP')
s = s.replace('MM PS5 WEB APP STUDIO', 'MM HTML2APP')
s = s.replace('MM_PS5_WEB_APP_STUDIO_V03_SELF_TEST', 'MM_HTML2APP_V011_SELF_TEST')

# Strict PS launcher title-id shape proven by PIZZA HEN: four letters + five digits.
validator = r'''def validate_title_id(tid):
    tid = tid.strip().upper()
    if not re.fullmatch(r"[A-Z]{4}[0-9]{5}", tid):
        raise ValueError("Media Title ID must be 4 uppercase letters + 5 digits (example MMHA00001).")
    return tid
'''
s = re.sub(r'def validate_title_id\(tid\):.*?(?=\ndef ensure_engine)', lambda m: validator.rstrip(), s, flags=re.S)

# One ordinary HTML file in. No project/folder semantics.
copy_block = r'''def copy_project(selected, stage_www):
    selected = Path(selected)
    if not selected.is_file() or selected.suffix.lower() not in (".html", ".htm"):
        raise ValueError("Select a normal .html or .htm file.")
    stage_www.mkdir(parents=True, exist_ok=True)
    target = stage_www / "index.html"
    shutil.copy2(selected, target)
    return target
'''
s = re.sub(r'def copy_project\(selected, stage_www\):.*?(?=\ndef patch_websrv_title)', lambda m: copy_block.rstrip(), s, flags=re.S)

# DualSense/browser compatibility is injected inline into the copied HTML.
inline_block = r'''def inject_runtime(index_path, mapping, right_stick_mouse=True):
    text = index_path.read_text(encoding="utf-8", errors="replace")
    marker = "MM_HTML2APP_DUALSENSE"
    if marker in text:
        return
    cfg = {"mapping": mapping, "rightStickMouse": bool(right_stick_mouse), "mouseSpeed": 14, "deadzone": 0.18}
    inline = ("\\n<!-- MM_HTML2APP_DUALSENSE -->\\n<script>\\n"
              "window.MMPS5_CONFIG = " + json.dumps(cfg, ensure_ascii=False) + ";\\n"
              + controller_runtime_js() + "\\n</script>\\n")
    m = re.search(r"</body\\s*>", text, flags=re.I)
    if m:
        text = text[:m.start()] + inline + text[m.start():]
    else:
        text += inline
    index_path.write_text(text, encoding="utf-8")
'''
s = re.sub(r'def inject_runtime\(index_path, mapping, right_stick_mouse=True\):.*?(?=\ndef copy_project)', lambda m: inline_block.rstrip(), s, flags=re.S)

# Replace the old incomplete installer with the same service/registration order used by the
# hardware-proven PIZZA HEN Media tile path: privilege raise, NetCtl + UserService,
# AppInstUtil init, TitleDir registration/fallback, terminate, retry, visible status.
bootstrap = r'''def build_bootstrap_source(title_id,app_name,asset_files,include_bg):
    declarations=[]; entries=[]
    for symbol,rel,dest in asset_files:
        declarations.append(f'INCASSET({symbol}, "{c_escape(rel)}");')
        entries.append(f'  {{"{c_escape(dest)}", {symbol}, &{symbol}_size}},')
    bg_decl='INCASSET(bg, "payload_assets/pic1.png");' if include_bg else ''
    bg_write=f'  if(install_file("/user/app/{title_id}/sce_sys/pic1.png",bg,bg_size)) return fail_stage("background_write_failed",-41);' if include_bg else ''
    return f'''#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <ps5/kernel.h>
#include "elfldr.h"

#define INCASSET(name, file) \\
  __asm__(".section .rodata\\n" \\
          ".global " #name "\\n" \\
          ".global " #name "_end\\n" \\
          ".global " #name "_size\\n" \\
          ".align 16\\n" \\
          #name ":\\n" \\
          ".incbin \\\"" file "\\\"\\n" \\
          #name "_end:\\n" \\
          #name "_size:\\n" \\
          ".quad " #name "_end - " #name "\\n" \\
          ".previous\\n"); \\
  extern const uint8_t name[]; \\
  extern const size_t name##_size;

INCASSET(websrv_elf, "payload_assets/websrv.elf");
INCASSET(param, "payload_assets/param.json");
INCASSET(icon, "payload_assets/icon0.png");
{bg_decl}
{os.linesep.join(declarations)}

typedef struct {{ const char* path; const uint8_t* data; const size_t* size; }} asset_entry_t;
static asset_entry_t app_assets[] = {{
{os.linesep.join(entries)}
  {{0,0,0}}
}};

typedef struct {{
  int32_t type, req_id, priority, msg_id, target_id, user_id, unk1, unk2, app_id, error_num, unk3;
  char use_icon_image_uri;
  char message[1024];
  char uri[1024];
  char unkstr[1024];
}} OrbisNotificationRequest;

int sceKernelSendNotificationRequest(int32_t, OrbisNotificationRequest*, size_t, int32_t);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void*);
int sceUserServiceInitialize(void*);
void sceUserServiceTerminate(void);
int sceNetCtlInit(void);

static void notify(const char* fmt, ...) {{
  OrbisNotificationRequest req; memset(&req,0,sizeof(req));
  va_list ap; va_start(ap,fmt); vsnprintf(req.message,sizeof(req.message),fmt,ap); va_end(ap);
  req.type=0; req.use_icon_image_uri=1; req.target_id=-1;
  snprintf(req.uri,sizeof(req.uri),"cxml://psnotification/tex_icon_system");
  sceKernelSendNotificationRequest(0,&req,sizeof(req),0);
  printf("MM HTML2APP: %s\\n",req.message);
}}

static int mkdir_p(const char* path) {{
  char tmp[1024]; size_t n=strlen(path); if(n>=sizeof(tmp)) return -1;
  memcpy(tmp,path,n+1);
  for(char* p=tmp+1;*p;++p) if(*p=='/') {{ *p=0; if(mkdir(tmp,0755)&&errno!=EEXIST) return -1; *p='/'; }}
  if(mkdir(tmp,0755)&&errno!=EEXIST) return -1; return 0;
}}

static int install_file(const char* path,const uint8_t* data,size_t size) {{
  char dir[1024]; if(strlen(path)>=sizeof(dir)) return -1;
  strcpy(dir,path); char* slash=strrchr(dir,'/'); if(slash) {{ *slash=0; if(mkdir_p(dir)) return -1; }}
  FILE* f=fopen(path,"wb"); if(!f) return -1;
  size_t done=fwrite(data,1,size,f); fflush(f); fsync(fileno(f)); fclose(f);
  return done==size?0:-1;
}}

static void media_status(const char* stage,int rc) {{
  mkdir_p("/data/MM_HTML2APP/runtime");
  char body[320];
  int n=snprintf(body,sizeof(body),"stage=%s\\nrc=0x%08X\\ntitleId={title_id}\\ncategory=65536\\ntitleIdFormat=AAAA99999\\n",stage,(unsigned)rc);
  if(n>0) install_file("/data/MM_HTML2APP/runtime/{title_id}_media_tile_status.txt",(const uint8_t*)body,(size_t)n);
}}

static int fail_stage(const char* stage,int rc) {{ media_status(stage,rc); return rc; }}

static int install_app(const char* title_id,const char* dir) {{
  int (*sceAppInstUtilAppInstallTitleDir)(const char*,const char*,void*)=0;
  uint32_t handle=0;
  if(!kernel_dynlib_handle(-1,"libSceAppInstUtil.sprx",&handle))
    sceAppInstUtilAppInstallTitleDir=(void*)kernel_dynlib_resolve(-1,handle,"Wudg3Xe3heE");
  if(sceAppInstUtilAppInstallTitleDir) return sceAppInstUtilAppInstallTitleDir(title_id,dir,0);
  return sceAppInstUtilAppInstallAll(0);
}}

static int register_media_once(void) {{
  int user_prio=256;
  int net_rc=sceNetCtlInit();
  int user_rc=sceUserServiceInitialize(&user_prio);
  printf("MM HTML2APP services: NetCtl=0x%X UserService=0x%X\\n",net_rc,user_rc);

  int rc=sceAppInstUtilInitialize();
  if(rc!=0) return fail_stage("appinst_init_failed",rc);

  if(mkdir_p("/user/app/{title_id}/sce_sys")) {{ rc=-30; goto done; }}
  if(install_file("/user/app/{title_id}/sce_sys/param.json",param,param_size)) {{ rc=-31; goto done; }}
  if(install_file("/user/app/{title_id}/sce_sys/icon0.png",icon,icon_size)) {{ rc=-32; goto done; }}
{bg_write}

  rc=install_app("{title_id}","/user/app/");
  if(rc==0) {{
    static const char marker[]="MM_HTML2APP_MEDIA_REGISTERED_V1\\n";
    install_file("/user/app/{title_id}/.mm_html2app_media_registered",(const uint8_t*)marker,sizeof(marker)-1);
    media_status("registered",0);
  }} else {{
    unlink("/user/app/{title_id}/.mm_html2app_media_registered");
    media_status("registration_failed",rc);
  }}

done:
  sceAppInstUtilTerminate();
  return rc;
}}

int main(void) {{
  signal(SIGCHLD,SIG_DFL);
  if(elfldr_raise_privileges(getpid())) {{
    notify("MM HTML2APP: privilege elevation failed");
    return fail_stage("privilege_failed",-10);
  }}
  kernel_set_ucred_authid(-1,0x4801000000000013L);
  notify("MM HTML2APP: installing {c_escape(app_name)}...");

  for(asset_entry_t* e=app_assets;e->path;++e) {{
    if(install_file(e->path,e->data,*e->size)) {{
      notify("MM HTML2APP: HTML write failed"); return fail_stage("html_write_failed",-20);
    }}
  }}

  int rc=register_media_once();
  if(rc!=0) {{ usleep(250000); rc=register_media_once(); }}
  if(rc!=0) {{
    notify("MM HTML2APP: Media icon install FAILED 0x%X - see /data/MM_HTML2APP/runtime/{title_id}_media_tile_status.txt",rc);
    return rc;
  }}

  char* argv[]={{"websrv.elf",0}}; char* envp[]={{0}};
  int pid=elfldr_spawn("/",STDOUT_FILENO,(uint8_t*)websrv_elf,argv,envp);
  if(pid<0) {{ notify("MM HTML2APP: websrv start FAILED"); return fail_stage("websrv_spawn_failed",-50); }}
  media_status("registered_websrv_started",0);
  notify("MM HTML2APP: {c_escape(app_name)} installed in Media");
  sleep(1);
  return 0;
}}
'''
'''
s = re.sub(r'def build_bootstrap_source\(title_id,app_name,asset_files,include_bg\):.*?(?=\ndef compile_payload)', lambda m: bootstrap.rstrip(), s, flags=re.S)

# Absolute output path + robust invocation of the official Windows SDK wrapper.
s = s.replace('outdir=Path(output_dir or (app_root()/"output")); outdir.mkdir(parents=True,exist_ok=True)',
              'outdir=Path(output_dir or (app_root()/"output")).resolve(); outdir.mkdir(parents=True,exist_ok=True)')
s = s.replace('output_elf=outdir/f"{slug}-{title_id}.elf"', 'output_elf=(outdir/f"{slug}-{title_id}.elf").resolve()')
old_compile = '''        cmdline=f'call "{sdk_win/"prospero-clang.cmd"}" bootstrap.c elfldr.c pt.c -I. -O2 -Wall -o "{output_elf}" -lkernel_sys -lSceAppInstUtil'\n        if progress: progress("Compiling PS5 bootstrap ELF...")\n        cp=subprocess.run(["cmd.exe","/d","/s","/c",cmdline],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors="replace")'''
new_compile = '''        wrapper=(sdk_win/"prospero-clang.cmd").resolve()\n        compile_bat=work/"compile-html2app.cmd"\n        compile_bat.write_text('@echo off\\r\\ncall "'+str(wrapper)+'" bootstrap.c elfldr.c pt.c -I. -O2 -Wall -o "'+str(output_elf)+'" -lkernel_sys -lSceAppInstUtil -lSceUserService -lSceNetCtl -lSceNotification\\r\\nexit /b %errorlevel%\\r\\n',encoding="utf-8")\n        if progress: progress("Compiling PS5 HTML2APP ELF...")\n        cp=subprocess.run(["cmd.exe","/d","/c",str(compile_bat)],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors="replace")'''
if old_compile not in s:
    raise SystemExit('compile invocation block not found')
s = s.replace(old_compile,new_compile)

# Media metadata: category 65536 is required by the hardware-proven PIZZA Media launcher.
s = s.replace('param={"titleId":title_id,"deeplinkUri":deeplink,',
              'param={"titleId":title_id,"applicationCategoryType":65536,"deeplinkUri":deeplink,')

# UI / terminology.
s = s.replace('self.project=tk.StringVar();self.appname=tk.StringVar(value="My PS5 Web App");self.titleid=tk.StringVar(value="MMWEB0001")',
              'self.project=tk.StringVar();self.appname=tk.StringVar(value="My HTML App");self.titleid=tk.StringVar(value="MMHA00001")')
s = s.replace('nb.add(main,text="Web App & Media")','nb.add(main,text="HTML & Media")')
s = s.replace('self.rowfile(main,0,"HTML file / project:",self.project,self.pick_html,"Select HTML");ttk.Button(main,text="Select Folder",command=self.pick_folder).grid(row=0,column=3,padx=4)',
              'self.rowfile(main,0,"HTML file:",self.project,self.pick_html,"Select HTML")')
s = s.replace('Media Title ID (9 chars)','Media Title ID (4 letters + 5 digits)')
s = s.replace('The generated ELF contains websrv v0.34 itself. Running that ELF on PS5 installs the Media tile, writes the web app under /data, starts websrv on :8080, and the tile deeplinks into the installed HTML.',
              'Choose one normal HTML file. MM HTML2APP installs a Media launcher using the same AppInstUtil sequence as the hardware-proven PIZZA HEN Media tile, then starts embedded websrv :8080. Registration failures are shown on the PS5 and written under /data/MM_HTML2APP/runtime/.')
s = s.replace('BUILD PS5 WEB APP ELF','BUILD PS5 APP ELF')
s = s.replace('Staging HTML/CSS/JS...','Staging HTML...')
s = s.replace('Gamepad API + browser key-event fallback are both injected. Console behavior remains hardware-test dependent.',
              'DualSense compatibility is inserted inline in the generated HTML. Your original HTML is not modified. Media registration and input remain hardware-test dependent.')

# Self-test identity and valid ID.
s = s.replace('MM Web App SelfTest','MM HTML2APP SelfTest')
s = s.replace('MMWEB0001','MMHA00001')
s = s.replace('MM WEB APP TEST','MM HTML2APP TEST')
s = s.replace('DUALSENSE_RUNTIME_INJECTED=PASS','DUALSENSE_INLINE_RUNTIME=PASS')
s = s.replace('Path("www")/f.relative_to(appassetroot)','Path("html")/f.relative_to(appassetroot)')

# Strengthen source-mode checks against the exact hardware fixes.
s = s.replace('(b"MM HTML2APP TEST" in b,"HTML embedded")]',
              '(b"MM HTML2APP TEST" in b,"HTML embedded"),(b"MM_HTML2APP_DUALSENSE" in b,"DualSense inline runtime"),(b"applicationCategoryType" in b,"Media category metadata"),(b"AAAA99999" in b,"Title ID format marker") ]')

out.write_text(s,encoding='utf-8')
print(out)
