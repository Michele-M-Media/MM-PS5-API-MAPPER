from pathlib import Path
import re

src_path = Path('.github/mmwebapp-v03/studio_v03.py')
out_path = Path('buildsrc/MM-HTML2APP.py')
out_path.parent.mkdir(parents=True, exist_ok=True)
s = src_path.read_text(encoding='utf-8')

# Product identity and persistent paths.
s = s.replace('APP_TITLE = "MM PS5 WEB APP STUDIO"', 'APP_TITLE = "MM HTML2APP"')
s = s.replace('VERSION = "v0.3 WEBsrv MEDIA ALL-IN-ONE"', 'VERSION = "v0.1 ALL-IN-ONE"')
s = s.replace('MM_WEB_APP_STUDIO', 'MM_HTML2APP')
s = s.replace('MM PS5 WEB APP STUDIO', 'MM HTML2APP')
s = s.replace('MM_PS5_WEB_APP_STUDIO_V03_SELF_TEST', 'MM_HTML2APP_V01_SELF_TEST')
s = s.replace('MMWEB asset install failed', 'MMHTML2APP asset install failed')
s = s.replace('MMWEB AppInstUtil result=', 'MMHTML2APP AppInstUtil result=')
s = s.replace('MMWEB websrv spawn failed', 'MMHTML2APP websrv spawn failed')
s = s.replace('MMWEB installed ', 'MMHTML2APP installed ')

# A normal single HTML file is the source format. No project folder is required.
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

# Inject controller compatibility directly inside index.html. The generated PS5 app therefore
# still consists of one HTML document from websrv's point of view.
inline_block = r'''def inject_runtime(index_path, mapping, right_stick_mouse=True):
    text = index_path.read_text(encoding="utf-8", errors="replace")
    marker = "MM_HTML2APP_DUALSENSE"
    if marker in text:
        return
    cfg = {"mapping": mapping, "rightStickMouse": bool(right_stick_mouse), "mouseSpeed": 14, "deadzone": 0.18}
    inline = ("\n<!-- MM_HTML2APP_DUALSENSE -->\n<script>\n"
              "window.MMPS5_CONFIG = " + json.dumps(cfg, ensure_ascii=False) + ";\n"
              + controller_runtime_js() + "\n</script>\n")
    m = re.search(r"</body\\s*>", text, flags=re.I)
    if m:
        text = text[:m.start()] + inline + text[m.start():]
    else:
        text += inline
    index_path.write_text(text, encoding="utf-8")
'''
s = re.sub(r'def inject_runtime\(index_path, mapping, right_stick_mouse=True\):.*?(?=\ndef copy_project)', lambda m: inline_block.rstrip(), s, flags=re.S)

# Output paths must remain valid while clang works from a temporary directory.
s = s.replace('outdir=Path(output_dir or (app_root()/"output")); outdir.mkdir(parents=True,exist_ok=True)',
              'outdir=Path(output_dir or (app_root()/"output")).resolve(); outdir.mkdir(parents=True,exist_ok=True)')
s = s.replace('output_elf=outdir/f"{slug}-{title_id}.elf"', 'output_elf=(outdir/f"{slug}-{title_id}.elf").resolve()')

# Robust invocation of the official Windows SDK wrapper. This avoids cmd.exe nested-quote issues.
old_compile = '''        cmdline=f'call "{sdk_win/"prospero-clang.cmd"}" bootstrap.c elfldr.c pt.c -I. -O2 -Wall -o "{output_elf}" -lkernel_sys -lSceAppInstUtil'\n        if progress: progress("Compiling PS5 bootstrap ELF...")\n        cp=subprocess.run(["cmd.exe","/d","/s","/c",cmdline],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors="replace")'''
new_compile = '''        wrapper=(sdk_win/"prospero-clang.cmd").resolve()\n        compile_bat=work/"compile-html2app.cmd"\n        compile_bat.write_text('@echo off\\r\\ncall "'+str(wrapper)+'" bootstrap.c elfldr.c pt.c -I. -O2 -Wall -o "'+str(output_elf)+'" -lkernel_sys -lSceAppInstUtil\\r\\nexit /b %errorlevel%\\r\\n',encoding="utf-8")\n        if progress: progress("Compiling PS5 HTML2APP ELF...")\n        cp=subprocess.run(["cmd.exe","/d","/c",str(compile_bat)],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors="replace")'''
if old_compile not in s:
    raise SystemExit('compile invocation block not found')
s = s.replace(old_compile, new_compile)

# Terminology/UI: one HTML in, one ELF out.
s = s.replace('self.project=tk.StringVar();self.appname=tk.StringVar(value="My PS5 Web App");self.titleid=tk.StringVar(value="MMWEB0001")',
              'self.project=tk.StringVar();self.appname=tk.StringVar(value="My HTML App");self.titleid=tk.StringVar(value="MMHTM0001")')
s = s.replace('nb.add(main,text="Web App & Media")', 'nb.add(main,text="HTML & Media")')
s = s.replace('self.rowfile(main,0,"HTML file / project:",self.project,self.pick_html,"Select HTML");ttk.Button(main,text="Select Folder",command=self.pick_folder).grid(row=0,column=3,padx=4)',
              'self.rowfile(main,0,"HTML file:",self.project,self.pick_html,"Select HTML")')
s = s.replace('The generated ELF contains websrv v0.34 itself. Running that ELF on PS5 installs the Media tile, writes the web app under /data, starts websrv on :8080, and the tile deeplinks into the installed HTML.',
              'Choose one normal HTML file. MM HTML2APP puts a PS5-ready copy into the websrv environment, installs the Media tile, starts websrv :8080 and opens that HTML from the tile. DualSense support is injected inside the HTML copy itself.')
s = s.replace('BUILD PS5 WEB APP ELF', 'BUILD PS5 APP ELF')
s = s.replace('Gamepad API + browser key-event fallback are both injected. Console behavior remains hardware-test dependent.',
              'DualSense compatibility is inserted inline in the generated HTML. Your original HTML file is never modified. Console behavior remains hardware-test dependent.')
s = s.replace('Staging HTML/CSS/JS...', 'Staging HTML...')
s = s.replace('My PS5 Web App', 'My HTML App')

# Self-test identity/default Title ID and expected inline runtime.
s = s.replace('MM Web App SelfTest', 'MM HTML2APP SelfTest')
s = s.replace('MMWEB0001', 'MMHTM0001')
s = s.replace('MM WEB APP TEST', 'MM HTML2APP TEST')
s = s.replace('DUALSENSE_RUNTIME_INJECTED=PASS', 'DUALSENSE_INLINE_RUNTIME=PASS')
s = s.replace('(b"MM HTML2APP TEST" in b,"HTML embedded")]', '(b"MM HTML2APP TEST" in b,"HTML embedded"),(b"MM_HTML2APP_DUALSENSE" in b,"DualSense inline runtime") ]')

# The source ZIP should describe a single HTML app rather than a web project.
s = s.replace('Path("www")/f.relative_to(appassetroot)', 'Path("html")/f.relative_to(appassetroot)')

out_path.write_text(s, encoding='utf-8')
print(out_path)
