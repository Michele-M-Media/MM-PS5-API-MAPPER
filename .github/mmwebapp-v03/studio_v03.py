import os, sys, re, json, shutil, tempfile, subprocess, socket, hashlib, zipfile, traceback
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

try:
    from PIL import Image, ImageOps, ImageDraw
except Exception:
    Image = None

APP_TITLE = "MM PS5 WEB APP STUDIO"
VERSION = "v0.3 WEBsrv MEDIA ALL-IN-ONE"
WEBSRV_VERSION = "v0.34"
SDK_VERSION = "v0.43"
LLVM_VERSION = "18.1.8"

def bundle_root():
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    return Path(__file__).resolve().parent

def app_root():
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent

ENGINE = bundle_root() / "engine"

DEFAULT_MAP = {
    "Cross": "Enter", "Circle": "Escape", "Square": " ", "Triangle": "Tab",
    "DPadUp": "ArrowUp", "DPadDown": "ArrowDown", "DPadLeft": "ArrowLeft", "DPadRight": "ArrowRight",
    "L1": "PageUp", "R1": "PageDown", "L2": "q", "R2": "e",
    "Options": "Escape", "Create": "Tab", "L3": "Shift", "R3": "Control",
}

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024*1024), b""):
            h.update(chunk)
    return h.hexdigest()

def safe_slug(s):
    s = re.sub(r"[^A-Za-z0-9._-]+", "-", s.strip()).strip("-")
    return s or "webapp"

def validate_title_id(tid):
    tid = tid.strip().upper()
    if not re.fullmatch(r"[A-Z0-9]{9}", tid):
        raise ValueError("Media Title ID must be exactly 9 characters: A-Z / 0-9 (example MMWEB0001).")
    return tid

def ensure_engine():
    req = [
        ENGINE/"sdk"/"win"/"prospero-clang.cmd",
        ENGINE/"sdk"/"win"/"prospero-lld.exe",
        ENGINE/"llvm"/"bin"/"clang.exe",
        ENGINE/"websrv"/"websrv-ps5.elf",
        ENGINE/"websrv"/"src"/"ps5"/"elfldr.c",
        ENGINE/"websrv"/"src"/"ps5"/"elfldr.h",
        ENGINE/"websrv"/"src"/"ps5"/"pt.c",
        ENGINE/"websrv"/"src"/"ps5"/"pt.h",
    ]
    missing = [str(p) for p in req if not p.exists()]
    if missing:
        raise RuntimeError("Embedded FULL build engine is incomplete:\n" + "\n".join(missing))

def create_default_icon(dst_path, size=(512,512)):
    if Image is None:
        Path(dst_path).write_bytes(bytes.fromhex(
            "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
            "0000000d49444154789c63606060f80f0001040100c89f5d7b0000000049454e44ae426082"))
        return
    im = Image.new("RGBA", size, (20,20,24,255))
    d = ImageDraw.Draw(im)
    w,h=size
    d.rounded_rectangle((w*.12,h*.12,w*.88,h*.88), radius=int(w*.12), outline=(245,245,245,255), width=max(4,int(w*.02)))
    d.text((w*.25,h*.34), "MM", fill=(245,245,245,255))
    d.text((w*.20,h*.58), "WEB APP", fill=(245,245,245,255))
    im.save(dst_path, "PNG")

def image_to_png(src_path, dst_path, size):
    if not src_path:
        create_default_icon(dst_path, size)
        return
    if Image is None:
        raise RuntimeError("Pillow image engine is not available.")
    im = Image.open(src_path).convert("RGBA")
    im = ImageOps.fit(im, size, method=Image.Resampling.LANCZOS)
    im.save(dst_path, "PNG", optimize=True)

def controller_runtime_js():
    return r'''(function(){
"use strict";
const C = window.MMPS5_CONFIG || {};
const M = C.mapping || {};
const DEAD = Number(C.deadzone || 0.18);
const SPEED = Number(C.mouseSpeed || 14);
const IDX = {Cross:0,Circle:1,Square:2,Triangle:3,L1:4,R1:5,L2:6,R2:7,Create:8,Options:9,L3:10,R3:11,DPadUp:12,DPadDown:13,DPadLeft:14,DPadRight:15};
const prev = {};
const synthetic = new WeakSet();
function target(){ return document.activeElement || document.body || document.documentElement; }
function keyEvent(type,key){
  if(!key) return;
  const ev = new KeyboardEvent(type,{key:key,code:key,bubbles:true,cancelable:true,composed:true});
  synthetic.add(ev);
  target().dispatchEvent(ev);
  window.dispatchEvent(ev);
}
function logicalPress(name, down){
  const k=M[name];
  if(k) keyEvent(down?"keydown":"keyup",k);
}
const nativeToLogical = {"Enter":"Cross","Escape":"Circle"," ":"Square","Spacebar":"Square","Tab":"Triangle","ArrowUp":"DPadUp","ArrowDown":"DPadDown","ArrowLeft":"DPadLeft","ArrowRight":"DPadRight"};
window.addEventListener("keydown", function(e){
  if(synthetic.has(e)) return;
  const n=nativeToLogical[e.key];
  if(!n || !M[n] || M[n]===e.key) return;
  e.preventDefault(); e.stopPropagation(); logicalPress(n,true);
}, true);
window.addEventListener("keyup", function(e){
  if(synthetic.has(e)) return;
  const n=nativeToLogical[e.key];
  if(!n || !M[n] || M[n]===e.key) return;
  e.preventDefault(); e.stopPropagation(); logicalPress(n,false);
}, true);
let cx=Math.floor(innerWidth/2), cy=Math.floor(innerHeight/2), cursor;
function ensureCursor(){
  if(cursor) return;
  cursor=document.createElement("div");
  cursor.id="mm-ps5-virtual-cursor";
  cursor.style.cssText="position:fixed;z-index:2147483647;width:18px;height:18px;border:2px solid white;border-radius:50%;background:rgba(0,0,0,.35);pointer-events:none;left:0;top:0;transform:translate(-50%,-50%);box-shadow:0 0 3px #000";
  document.documentElement.appendChild(cursor);
}
function moveCursor(dx,dy){
  ensureCursor();
  cx=Math.max(0,Math.min(innerWidth-1,cx+dx));
  cy=Math.max(0,Math.min(innerHeight-1,cy+dy));
  cursor.style.left=cx+"px"; cursor.style.top=cy+"px";
  const el=document.elementFromPoint(cx,cy);
  if(el) el.dispatchEvent(new MouseEvent("mousemove",{clientX:cx,clientY:cy,bubbles:true}));
}
function clickCursor(){
  ensureCursor();
  const el=document.elementFromPoint(cx,cy);
  if(!el) return;
  ["pointerdown","mousedown","pointerup","mouseup","click"].forEach(t=>{
    let ev;
    try { ev=t.indexOf("pointer")===0 ? new PointerEvent(t,{clientX:cx,clientY:cy,bubbles:true,button:0}) : new MouseEvent(t,{clientX:cx,clientY:cy,bubbles:true,button:0}); }
    catch(_) { ev=new MouseEvent(t,{clientX:cx,clientY:cy,bubbles:true,button:0}); }
    el.dispatchEvent(ev);
  });
}
function poll(){
  let gps=[];
  try{ gps=navigator.getGamepads ? navigator.getGamepads() : []; }catch(_){}
  const g=gps && Array.from(gps).find(Boolean);
  if(g){
    Object.keys(IDX).forEach(name=>{
      const b=g.buttons && g.buttons[IDX[name]];
      const down=!!(b && (b.pressed || b.value>0.5));
      if(prev[name]!==down){
        prev[name]=down;
        if(name==="Cross" && C.rightStickMouse && down) clickCursor();
        logicalPress(name,down);
      }
    });
    if(C.rightStickMouse && g.axes && g.axes.length>=4){
      let x=g.axes[2]||0, y=g.axes[3]||0;
      if(Math.abs(x)<DEAD) x=0; if(Math.abs(y)<DEAD) y=0;
      if(x||y) moveCursor(x*SPEED,y*SPEED);
    }
  }
  requestAnimationFrame(poll);
}
requestAnimationFrame(poll);
})();'''

def inject_runtime(index_path, mapping, right_stick_mouse=True):
    text = index_path.read_text(encoding="utf-8", errors="replace")
    marker = "MM_PS5_WEB_APP_STUDIO_RUNTIME"
    if marker not in text:
        tags = '\n<!-- MM_PS5_WEB_APP_STUDIO_RUNTIME -->\n<script src="mm_ps5_config.js"></script>\n<script src="mm_ps5_input.js"></script>\n'
        m = re.search(r"</body\s*>", text, flags=re.I)
        if m: text = text[:m.start()] + tags + text[m.start():]
        else: text += tags
        index_path.write_text(text, encoding="utf-8")
    cfg = {"mapping":mapping,"rightStickMouse":bool(right_stick_mouse),"mouseSpeed":14,"deadzone":0.18}
    (index_path.parent/"mm_ps5_config.js").write_text("window.MMPS5_CONFIG = "+json.dumps(cfg,ensure_ascii=False)+";\n",encoding="utf-8")
    (index_path.parent/"mm_ps5_input.js").write_text(controller_runtime_js(),encoding="utf-8")

def copy_project(selected, stage_www):
    selected = Path(selected)
    if selected.is_file():
        if selected.suffix.lower() not in (".html",".htm"): raise ValueError("Select an .html or .htm file.")
        root=selected.parent; entry=selected
    elif selected.is_dir():
        root=selected
        entry=next((p for p in (root/"index.html",root/"index.htm") if p.exists()),None)
        if entry is None:
            htmls=list(root.glob("*.html"))+list(root.glob("*.htm"))
            if len(htmls)==1: entry=htmls[0]
            else: raise ValueError("Folder must contain index.html/index.htm (or exactly one HTML file).")
    else: raise ValueError("HTML project path does not exist.")
    shutil.copytree(root,stage_www,dirs_exist_ok=True)
    copied=stage_www/entry.name
    if copied.name.lower()!="index.html":
        target=stage_www/"index.html"
        if target.exists() and target!=copied: target.unlink()
        copied.rename(target); copied=target
    return copied

def patch_websrv_title(data,title_id):
    old=b"FAKE00000"; new=title_id.encode("ascii")
    count=data.count(old)
    if count<1: raise RuntimeError("Embedded websrv does not contain expected FAKE00000 marker.")
    return data.replace(old,new),count

def c_escape(s): return s.replace("\\","\\\\").replace('"','\\"')

def build_bootstrap_source(title_id,app_name,asset_files,include_bg):
    declarations=[]; entries=[]
    for symbol,rel,dest in asset_files:
        declarations.append(f'INCASSET({symbol}, "{c_escape(rel)}");')
        entries.append(f'  {{"{c_escape(dest)}", {symbol}, &{symbol}_size}},')
    bg_decl='INCASSET(bg, "payload_assets/pic1.png");' if include_bg else ''
    bg_line=f'  if(install_file("/user/app/{title_id}/sce_sys/pic1.png",bg,bg_size)) return -41;' if include_bg else ''
    return f'''#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void*);

static int mkdir_p(const char* path) {{
  char tmp[1024]; size_t n=strlen(path);
  if(n>=sizeof(tmp)) return -1;
  memcpy(tmp,path,n+1);
  for(char* p=tmp+1;*p;++p) {{
    if(*p=='/') {{ *p=0; if(mkdir(tmp,0755)&&errno!=EEXIST) return -1; *p='/'; }}
  }}
  if(mkdir(tmp,0755)&&errno!=EEXIST) return -1;
  return 0;
}}

static int install_file(const char* path,const uint8_t* data,size_t size) {{
  char dir[1024];
  if(strlen(path)>=sizeof(dir)) return -1;
  strcpy(dir,path); char* slash=strrchr(dir,'/');
  if(slash) {{ *slash=0; if(mkdir_p(dir)) return -1; }}
  FILE* f=fopen(path,"wb"); if(!f) return -1;
  size_t done=fwrite(data,1,size,f); fclose(f);
  return done==size?0:-1;
}}

static int install_app(const char* title_id,const char* dir) {{
  int (*sceAppInstUtilAppInstallTitleDir)(const char*,const char*,void*)=0;
  const char* nid="Wudg3Xe3heE"; uint32_t handle;
  if(!kernel_dynlib_handle(-1,"libSceAppInstUtil.sprx",&handle))
    sceAppInstUtilAppInstallTitleDir=(void*)kernel_dynlib_resolve(-1,handle,nid);
  if(sceAppInstUtilAppInstallTitleDir) return sceAppInstUtilAppInstallTitleDir(title_id,dir,0);
  return sceAppInstUtilAppInstallAll(0);
}}

int main(void) {{
  kernel_set_ucred_authid(-1,0x4801000000000013L);
  for(asset_entry_t* e=app_assets;e->path;++e) {{
    if(install_file(e->path,e->data,*e->size)) {{
      printf("MMWEB asset install failed: %s\\n",e->path); return -20;
    }}
  }}
  if(mkdir_p("/user/app/{title_id}/sce_sys")) return -30;
  if(install_file("/user/app/{title_id}/sce_sys/param.json",param,param_size)) return -31;
  if(install_file("/user/app/{title_id}/sce_sys/icon0.png",icon,icon_size)) return -32;
{bg_line}
  if(!sceAppInstUtilInitialize()) {{
    int r=install_app("{title_id}","/user/app/");
    printf("MMWEB AppInstUtil result=%d\\n",r);
  }}
  char* argv[]={{"websrv.elf",0}}; char* envp[]={{0}};
  pid_t pid=elfldr_spawn("/",-1,(uint8_t*)websrv_elf,argv,envp);
  if(pid<0) {{ puts("MMWEB websrv spawn failed"); return -50; }}
  printf("MMWEB installed {c_escape(app_name)} as {title_id}, websrv pid=%d\\n",(int)pid);
  sleep(1); return 0;
}}
'''

def compile_payload(project_path,app_name,title_id,icon_path="",background_path="",mapping=None,right_stick_mouse=True,output_dir=None,progress=None):
    ensure_engine(); title_id=validate_title_id(title_id); mapping=mapping or dict(DEFAULT_MAP)
    outdir=Path(output_dir or (app_root()/"output")); outdir.mkdir(parents=True,exist_ok=True)
    slug=safe_slug(app_name); work=Path(tempfile.mkdtemp(prefix="mmps5web_"))
    try:
        if progress: progress("Staging HTML/CSS/JS...")
        www=work/"payload_assets"/"www"; www.mkdir(parents=True)
        index=copy_project(project_path,www); inject_runtime(index,mapping,right_stick_mouse)
        if progress: progress("Embedding official websrv v0.34...")
        websrv_orig=(ENGINE/"websrv"/"websrv-ps5.elf").read_bytes()
        websrv_patched,patch_count=patch_websrv_title(websrv_orig,title_id)
        (work/"payload_assets"/"websrv.elf").write_bytes(websrv_patched)
        deeplink=f"http://127.0.0.1:8080/fs/data/MM_WEB_APP_STUDIO/apps/{title_id}/www/index.html"
        param={"titleId":title_id,"deeplinkUri":deeplink,"localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":app_name}}}
        (work/"payload_assets"/"param.json").write_text(json.dumps(param,ensure_ascii=False,indent=4),encoding="utf-8")
        image_to_png(icon_path,work/"payload_assets"/"icon0.png",(512,512))
        include_bg=bool(background_path)
        if include_bg: image_to_png(background_path,work/"payload_assets"/"pic1.png",(1920,1080))
        appassetroot=work/"payload_assets"/"app"; shutil.copytree(www,appassetroot,dirs_exist_ok=True)
        asset_files=[]; i=0
        for f in sorted(p for p in appassetroot.rglob("*") if p.is_file()):
            rel=f.relative_to(work).as_posix(); sub=f.relative_to(appassetroot).as_posix()
            dest=f"/data/MM_WEB_APP_STUDIO/apps/{title_id}/www/{sub}"
            asset_files.append((f"appasset_{i}",rel,dest)); i+=1
        (work/"bootstrap.c").write_text(build_bootstrap_source(title_id,app_name,asset_files,include_bg),encoding="utf-8")
        for fn in ("elfldr.c","elfldr.h","pt.c","pt.h"):
            shutil.copy2(ENGINE/"websrv"/"src"/"ps5"/fn,work/fn)
        sdk=(ENGINE/"sdk").resolve(); llvm_bin=(ENGINE/"llvm"/"bin").resolve(); sdk_win=(sdk/"win").resolve()
        env=os.environ.copy(); env["PS5_PAYLOAD_SDK"]=str(sdk)
        env["PATH"]=str(llvm_bin)+os.pathsep+str(sdk_win)+os.pathsep+env.get("PATH","")
        output_elf=outdir/f"{slug}-{title_id}.elf"
        cmdline=f'call "{sdk_win/"prospero-clang.cmd"}" bootstrap.c elfldr.c pt.c -I. -O2 -Wall -o "{output_elf}" -lkernel_sys -lSceAppInstUtil'
        if progress: progress("Compiling PS5 bootstrap ELF...")
        cp=subprocess.run(["cmd.exe","/d","/s","/c",cmdline],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors="replace")
        log_path=outdir/f"{slug}-{title_id}.build.log"; log_path.write_text(cp.stdout,encoding="utf-8")
        if cp.returncode!=0 or not output_elf.exists():
            raise RuntimeError("PS5 compiler failed. See:\n"+str(log_path)+"\n\n"+cp.stdout[-5000:])
        b=output_elf.read_bytes()
        if len(b)<64 or b[:4]!=b"\x7fELF" or b[18:20]!=b"\x3e\x00": raise RuntimeError("Compiler output is not valid ELF64 x86-64.")
        source_zip=outdir/f"{slug}-{title_id}-SOURCE.zip"
        with zipfile.ZipFile(source_zip,"w",zipfile.ZIP_DEFLATED) as z:
            z.write(work/"bootstrap.c","bootstrap.c"); z.write(work/"payload_assets"/"param.json","param.json"); z.write(work/"payload_assets"/"icon0.png","icon0.png")
            if include_bg:z.write(work/"payload_assets"/"pic1.png","pic1.png")
            for f in sorted(p for p in appassetroot.rglob("*") if p.is_file()): z.write(f,Path("www")/f.relative_to(appassetroot))
            lic=ENGINE/"websrv"/"LICENSE"
            if lic.exists():z.write(lic,"THIRD_PARTY/websrv-LICENSE.txt")
        info={"studio":VERSION,"appName":app_name,"titleId":title_id,"deeplinkUri":deeplink,"websrv":WEBSRV_VERSION,"websrvOriginalSha256":sha256(ENGINE/"websrv"/"websrv-ps5.elf"),"websrvTitleMarkerPatchCount":patch_count,"elf":output_elf.name,"elfSha256":sha256(output_elf),"elfSize":output_elf.stat().st_size,"hostValidation":"ELF64 x86-64 + embedded websrv/bootstrap compile PASS; PS5 hardware not yet validated"}
        info_path=outdir/f"{slug}-{title_id}.build-info.json"; info_path.write_text(json.dumps(info,indent=2),encoding="utf-8")
        if progress:progress("BUILD PASS: "+output_elf.name)
        return output_elf,info_path,source_zip
    finally: shutil.rmtree(work,ignore_errors=True)

def send_elf(path,host,port=9021,progress=None):
    data=Path(path).read_bytes()
    if progress:progress(f"Connecting to {host}:{port}...")
    with socket.create_connection((host,int(port)),timeout=10) as s:s.sendall(data)
    if progress:progress(f"Sent {len(data):,} bytes.")

def run_self_test(out=None):
    out=Path(out or (app_root()/"selftest-v03"))
    if out.exists():shutil.rmtree(out)
    out.mkdir(parents=True); sample=out/"sample"; sample.mkdir()
    (sample/"index.html").write_text("<!doctype html><html><body><h1>MM WEB APP TEST</h1><script>document.addEventListener('keydown',e=>document.body.dataset.key=e.key);</script></body></html>",encoding="utf-8")
    icon=out/"icon.png";create_default_icon(icon,(512,512))
    try:
        elf,info,srczip=compile_payload(sample/"index.html","MM Web App SelfTest","MMWEB0001",str(icon),"",dict(DEFAULT_MAP),True,out,print)
        b=elf.read_bytes(); checks=[(b[:4]==b"\x7fELF","ELF magic"),(b[18:20]==b"\x3e\x00","x86_64"),(b"MMWEB0001" in b,"title id"),(b"FAKE00000" not in b,"websrv marker patch"),(b"/data/MM_WEB_APP_STUDIO/apps/MMWEB0001/www/index.html" in b,"install path"),(b"http://127.0.0.1:8080/fs/data/MM_WEB_APP_STUDIO/apps/MMWEB0001/www/index.html" in b,"deeplink"),(b"MM WEB APP TEST" in b,"HTML embedded")]
        bad=[n for ok,n in checks if not ok]
        if bad:raise RuntimeError("Self-test validation failed: "+", ".join(bad))
        report=out/"SELF-TEST-PASS.txt"; report.write_text("MM_PS5_WEB_APP_STUDIO_V03_SELF_TEST=PASS\nELF_SHA256="+sha256(elf)+"\nWEBSRV_EMBEDDED=PASS\nMEDIA_INSTALLER_BOOTSTRAP=PASS\nHTML_EMBEDDED=PASS\nDUALSENSE_RUNTIME_INJECTED=PASS\nHARDWARE_TEST=NOT_PERFORMED\n",encoding="utf-8")
        print(report.read_text());return 0
    except Exception:
        (out/"SELF-TEST-FAIL.txt").write_text(traceback.format_exc(),encoding="utf-8"); print(traceback.format_exc(),file=sys.stderr);return 1

class Studio(tk.Tk):
    def __init__(self):
        super().__init__();self.title(f"{APP_TITLE} {VERSION}");self.geometry("1040x780");self.minsize(900,650)
        self.project=tk.StringVar();self.appname=tk.StringVar(value="My PS5 Web App");self.titleid=tk.StringVar(value="MMWEB0001");self.icon=tk.StringVar();self.bg=tk.StringVar();self.ps5ip=tk.StringVar();self.port=tk.StringVar(value="9021");self.mouse=tk.BooleanVar(value=True);self.mapvars={k:tk.StringVar(value=v) for k,v in DEFAULT_MAP.items()};self.last_elf=None;self.build_ui()
    def rowfile(self,parent,row,label,var,cmd,button):
        ttk.Label(parent,text=label).grid(row=row,column=0,sticky="w",padx=8,pady=6);ttk.Entry(parent,textvariable=var).grid(row=row,column=1,sticky="ew",padx=8,pady=6);ttk.Button(parent,text=button,command=cmd).grid(row=row,column=2,padx=8,pady=6)
    def build_ui(self):
        nb=ttk.Notebook(self);nb.pack(fill="both",expand=True,padx=10,pady=10);main=ttk.Frame(nb);ctrl=ttk.Frame(nb);deploy=ttk.Frame(nb);nb.add(main,text="Web App & Media");nb.add(ctrl,text="DualSense Mapper");nb.add(deploy,text="Build / PS5");main.columnconfigure(1,weight=1)
        self.rowfile(main,0,"HTML file / project:",self.project,self.pick_html,"Select HTML");ttk.Button(main,text="Select Folder",command=self.pick_folder).grid(row=0,column=3,padx=4)
        ttk.Label(main,text="App name").grid(row=1,column=0,sticky="w",padx=8,pady=6);ttk.Entry(main,textvariable=self.appname).grid(row=1,column=1,sticky="ew",padx=8,pady=6)
        ttk.Label(main,text="Media Title ID (9 chars)").grid(row=2,column=0,sticky="w",padx=8,pady=6);ttk.Entry(main,textvariable=self.titleid,width=20).grid(row=2,column=1,sticky="w",padx=8,pady=6)
        self.rowfile(main,3,"Media icon:",self.icon,self.pick_icon,"Select Image");self.rowfile(main,4,"Background (optional):",self.bg,self.pick_bg,"Select Image")
        ttk.Label(main,text="The generated ELF contains websrv v0.34 itself. Running that ELF on PS5 installs the Media tile, writes the web app under /data, starts websrv on :8080, and the tile deeplinks into the installed HTML.",wraplength=860,justify="left").grid(row=6,column=0,columnspan=4,sticky="w",padx=8,pady=16)
        ctrl.columnconfigure(1,weight=1);ctrl.columnconfigure(3,weight=1);keys=["Enter","Escape"," ","Tab","ArrowUp","ArrowDown","ArrowLeft","ArrowRight","PageUp","PageDown","q","e","Shift","Control","a","d","w","s"]
        for i,name in enumerate(DEFAULT_MAP):
            r=i//2;c=(i%2)*2;ttk.Label(ctrl,text=name).grid(row=r,column=c,sticky="e",padx=8,pady=5);ttk.Combobox(ctrl,textvariable=self.mapvars[name],values=keys,width=18).grid(row=r,column=c+1,sticky="ew",padx=8,pady=5)
        ttk.Checkbutton(ctrl,text="Right Stick = virtual mouse / Cross = click",variable=self.mouse).grid(row=9,column=0,columnspan=4,sticky="w",padx=8,pady=15);ttk.Label(ctrl,text="Gamepad API + browser key-event fallback are both injected. Console behavior remains hardware-test dependent.",wraplength=850).grid(row=10,column=0,columnspan=4,sticky="w",padx=8)
        deploy.columnconfigure(1,weight=1);deploy.rowconfigure(6,weight=1);ttk.Label(deploy,text="PS5 IP").grid(row=0,column=0,sticky="w",padx=8,pady=6);ttk.Entry(deploy,textvariable=self.ps5ip).grid(row=0,column=1,sticky="ew",padx=8,pady=6);ttk.Label(deploy,text="ELF loader port").grid(row=1,column=0,sticky="w",padx=8,pady=6);ttk.Entry(deploy,textvariable=self.port,width=10).grid(row=1,column=1,sticky="w",padx=8,pady=6)
        ttk.Button(deploy,text="BUILD PS5 WEB APP ELF",command=self.build).grid(row=2,column=0,columnspan=2,sticky="ew",padx=8,pady=10);ttk.Button(deploy,text="BUILD + SEND TO PS5",command=lambda:self.build(send=True)).grid(row=3,column=0,columnspan=2,sticky="ew",padx=8,pady=4);ttk.Button(deploy,text="SEND LAST ELF",command=self.send_last).grid(row=4,column=0,columnspan=2,sticky="ew",padx=8,pady=4);ttk.Button(deploy,text="Open Output",command=self.open_output).grid(row=5,column=0,columnspan=2,sticky="ew",padx=8,pady=4)
        self.status=tk.Text(deploy,height=18,wrap="word");self.status.grid(row=6,column=0,columnspan=2,sticky="nsew",padx=8,pady=12);self.log(f"{APP_TITLE} {VERSION}\nEmbedded: websrv {WEBSRV_VERSION} | SDK {SDK_VERSION} | LLVM {LLVM_VERSION}")
    def log(self,s):self.status.insert("end",str(s)+"\n");self.status.see("end");self.update_idletasks()
    def pick_html(self):
        p=filedialog.askopenfilename(filetypes=[("HTML","*.html *.htm"),("All files","*.*")]);
        if p:self.project.set(p)
    def pick_folder(self):
        p=filedialog.askdirectory();
        if p:self.project.set(p)
    def pick_icon(self):
        p=filedialog.askopenfilename(filetypes=[("Images","*.png *.jpg *.jpeg *.webp *.bmp"),("All files","*.*")]);
        if p:self.icon.set(p)
    def pick_bg(self):
        p=filedialog.askopenfilename(filetypes=[("Images","*.png *.jpg *.jpeg *.webp *.bmp"),("All files","*.*")]);
        if p:self.bg.set(p)
    def mapping(self):return {k:v.get() for k,v in self.mapvars.items()}
    def build(self,send=False):
        try:
            elf,info,srcz=compile_payload(self.project.get(),self.appname.get(),self.titleid.get(),self.icon.get(),self.bg.get(),self.mapping(),self.mouse.get(),app_root()/"output",self.log);self.last_elf=elf;self.log("ELF SHA256: "+sha256(elf))
            if send:
                host=self.ps5ip.get().strip()
                if not host:raise ValueError("Enter PS5 IP.")
                send_elf(elf,host,int(self.port.get()),self.log);messagebox.showinfo("Sent","ELF built and sent. Now the PS5 hardware result decides Media/websrv/input PASS.")
            else:messagebox.showinfo("Build PASS",f"Generated:\n{elf}")
        except Exception as e:self.log("ERROR: "+str(e));messagebox.showerror("Build failed",str(e))
    def send_last(self):
        try:
            if not self.last_elf or not Path(self.last_elf).exists():raise ValueError("Build an ELF first.")
            if not self.ps5ip.get().strip():raise ValueError("Enter PS5 IP.")
            send_elf(self.last_elf,self.ps5ip.get().strip(),int(self.port.get()),self.log)
        except Exception as e:self.log("ERROR: "+str(e));messagebox.showerror("Send failed",str(e))
    def open_output(self):
        p=app_root()/"output";p.mkdir(exist_ok=True);os.startfile(str(p))

def main():
    if "--self-test" in sys.argv:
        out=None
        if "--self-test-output" in sys.argv:
            i=sys.argv.index("--self-test-output")
            if i+1<len(sys.argv):out=sys.argv[i+1]
        return run_self_test(out)
    Studio().mainloop();return 0

if __name__=="__main__": raise SystemExit(main())
